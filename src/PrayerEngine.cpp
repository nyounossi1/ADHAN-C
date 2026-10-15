/*
 * PrayerEngine.cpp — Prayer time fetching, parsing, next-prayer, qibla.
 */
#include "PrayerEngine.h"
#include "LocationQuery.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <stdio.h>

// ============================================================================
// Manual location (AD-50): town -> Open-Meteo, UK postcode -> postcodes.io
// ============================================================================
// GET an HTTPS JSON document. Returns the HTTP code (<= 0 on connection errors).
static int httpsGetJson(const String& url, JsonDocument& doc, JsonDocument& filter) {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(12);
  HTTPClient http;
  http.setTimeout(12000);
  http.setConnectTimeout(8000);
  http.setUserAgent(String("Adhan-AI/") + FW_VER);
  http.useHTTP10(true);
  if (!http.begin(client, url)) return -1;
  const int code = http.GET();
  if (code == HTTP_CODE_OK) {
    if (deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))) {
      http.end();
      return -2;
    }
  }
  http.end();
  return code;
}

// 1 = found, 0 = not found, -1 = lookup failed (retry later).
static int geocodeTown(const String& q, const char* preferCc, double& lat, double& lon, String& name) {
  char enc[96];
  if (!lqUrlEncode(q.c_str(), enc, sizeof(enc))) return 0;
  JsonDocument filter;
  JsonObject f = filter["results"].add<JsonObject>();
  f["name"] = true; f["admin1"] = true; f["country_code"] = true;
  f["latitude"] = true; f["longitude"] = true;
  JsonDocument doc;
  const int code = httpsGetJson(String("https://geocoding-api.open-meteo.com/v1/search?count=5&language=en&format=json&name=") + enc, doc, filter);
  if (code != HTTP_CODE_OK) return (code >= 400 && code < 500) ? 0 : -1;

  JsonArray res = doc["results"].as<JsonArray>();
  const char* cc[5] = { nullptr };
  int n = 0;
  for (JsonObject r : res) { if (n < 5) cc[n++] = r["country_code"] | ""; }
  const int i = lqPickResult(cc, n, preferCc);
  if (i < 0) return 0;
  JsonObject r = res[i];
  lat = r["latitude"] | 0.0;
  lon = r["longitude"] | 0.0;
  name = String((const char*)(r["name"] | "?"));
  const char* a1 = r["admin1"] | "";
  if (a1[0]) { name += ", "; name += a1; }
  if (cc[i] && cc[i][0]) { name += ", "; name += cc[i]; }
  return 1;
}

static int geocodePostcode(const char* compact, bool full, double& lat, double& lon, String& name) {
  JsonDocument filter;
  filter["result"]["latitude"] = true; filter["result"]["longitude"] = true;
  filter["result"]["admin_district"] = true;
  JsonDocument doc;
  const int code = httpsGetJson(String("https://api.postcodes.io/") + (full ? "postcodes/" : "outcodes/") + compact, doc, filter);
  if (code == 404) return 0;
  if (code != HTTP_CODE_OK) return -1;
  JsonVariant r = doc["result"];
  if (r.isNull()) return 0;
  lat = r["latitude"] | 0.0;
  lon = r["longitude"] | 0.0;
  // admin_district is a string for postcodes and an array for outcodes.
  const char* district = r["admin_district"].is<JsonArray>() ? (r["admin_district"][0] | "") : (r["admin_district"] | "");
  name = String(compact);
  if (full && strlen(compact) > 3) name = name.substring(0, name.length() - 3) + " " + name.substring(name.length() - 3);
  if (district[0]) { name += ", "; name += district; }
  name += ", GB";
  return 1;
}

// Resolve a pending manual query once online. Not found -> back to Auto.
static void resolveManualLocation(const char* ipCountry) {
  if (g_locMode != LOC_MANUAL || g_locQuery.length() == 0) return;
  const String q = g_locQuery;
  double lat = 0, lon = 0;
  String name;
  int found;
  bool full = false;
  char compact[16];
  const LocQueryKind kind = lqClassify(q.c_str());
  if (kind == LQ_LATLON) {
    lqParseLatLon(q.c_str(), lat, lon);
    char buf[40];
    snprintf(buf, sizeof(buf), "%.4f, %.4f", lat, lon);
    name = buf;
    found = 1;
  } else if (kind == LQ_POSTCODE && lqIsUkPostcode(q.c_str(), full, compact, sizeof(compact))) {
    found = geocodePostcode(compact, full, lat, lon, name);
  } else {
    found = geocodeTown(q, ipCountry, lat, lon, name);
  }

  if (found == 1) {
    g_locHaveCoords = true; g_locLat = lat; g_locLon = lon;
    g_locName = name; g_locQuery = ""; g_locMsg = "Using " + name;
    LOGI(LOG_TAG_LOC, "Manual location '%s' -> %s (%.5f, %.5f)", q.c_str(), name.c_str(), lat, lon);
  } else if (found == 0) {
    g_locMode = LOC_AUTO; g_locHaveCoords = false; g_locName = ""; g_locQuery = "";
    g_locMsg = "Not found: " + q + ", using auto";
    LOGW(LOG_TAG_LOC, "Manual location '%s' not found, back to auto", q.c_str());
  } else {
    g_locMsg = "Lookup failed, will retry: " + q;
    LOGW(LOG_TAG_LOC, "Manual location lookup failed for '%s', will retry", q.c_str());
  }
  saveSettings();
  g_settingsDirty = false;
}

// ============================================================================
// Location fetch (ip-api.com)
// ============================================================================
bool fetchLocationFromWifi() {
  updateSplashStatus("Fetching Location...");
  if (WiFi.status() != WL_CONNECTED) { updateSplashStatus("Location Error"); return false; }

  WiFiClient wifiClient;
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(wifiClient, "http://ip-api.com/json")) {
    LOGE(LOG_TAG_LOC, "HTTP begin failed"); updateSplashStatus("Location Error"); return false;
  }

  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    LOGE(LOG_TAG_LOC, "HTTP code=%d", code); http.end(); updateSplashStatus("Location Error"); return false;
  }

  const String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) { LOGE(LOG_TAG_LOC, "JSON parse error: %s", err.c_str()); updateSplashStatus("Location Error"); return false; }

  const double ipLat = doc["lat"] | 0.0;
  const double ipLon = doc["lon"] | 0.0;
  const char* ipTz = doc["timezone"] | "UTC";
  const char* ipCc = doc["countryCode"] | "";

  const String chosenIana = (g_tzOverride.length() > 0) ? g_tzOverride : String(ipTz);

  // AD-50: a manual location always wins over ip-api's lat/lon (timezone still from ip-api).
  resolveManualLocation(ipCc);
  const bool manual = lqUseManual(g_locMode, g_locHaveCoords);
  const double lat = manual ? g_locLat : ipLat;
  const double lon = manual ? g_locLon : ipLon;

  xSemaphoreTake(g_dataMtx, portMAX_DELAY);
  const bool moved = g_locationReady && (fabs(g_lat - lat) > 0.005 || fabs(g_lon - lon) > 0.005);
  g_lat = lat; g_lon = lon; g_tzIana = chosenIana; g_locationReady = true;
  xSemaphoreGive(g_dataMtx);
  if (moved) g_prayerRefetch = true;   // prayer times were for the old location

  applyTimezonePosix(chosenIana);
  sendUi(UI_EVT_LOCATION_READY);

  LOGI(LOG_TAG_LOC, "Location lat=%.6f lon=%.6f (%s) tz(ip)=%s tz(chosen)=%s", lat, lon,
       manual ? g_locName.c_str() : "ip-api", ipTz, chosenIana.c_str());
  updateSplashStatus("Location Ready");
  return true;
}

// ============================================================================
// Prayer fetch (Aladhan API)
// ============================================================================
bool fetchPrayerForDate(const String& dateStr, PrayerTimesDay& out) {
  if (WiFi.status() != WL_CONNECTED) { updateSplashStatus("Prayers: Error"); return false; }

  double lat, lon; String tz;
  xSemaphoreTake(g_dataMtx, portMAX_DELAY);
  lat = g_lat; lon = g_lon; tz = g_tzIana;
  xSemaphoreGive(g_dataMtx);

  updateSplashStatus("Fetching Prayers...");

  // The date must be in the path: Aladhan ignores a "date" query parameter and
  // returns today's times, which made "tomorrow" a copy of today.
  String path = "/v1/timings/" + dateStr +
                "?latitude=" + String(lat, 6) +
                "&longitude=" + String(lon, 6) +
                "&method=" + String(g_method) +
                "&school=" + String(g_school) +
                "&latitudeAdjustmentMethod=" + String(g_latAdj) +
                "&timezonestring=" + tz;

  String httpsUrl = String("https://api.aladhan.com") + path;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(12);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.setTimeout(12000);
  http.setConnectTimeout(8000);
  http.setUserAgent(String("Adhan-AI/") + FW_VER);
  http.useHTTP10(true);

  if (!http.begin(client, httpsUrl)) {
    LOGE(LOG_TAG_PRAY, "begin() failed"); updateSplashStatus("Prayers: Error"); return false;
  }

  http.addHeader("Connection", "close");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    LOGE(LOG_TAG_PRAY, "HTTP code=%d", code); http.end(); updateSplashStatus("Prayers: Error"); return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  auto err = deserializeJson(doc, payload);
  if (err) { LOGE(LOG_TAG_PRAY, "JSON error: %s", err.c_str()); updateSplashStatus("Prayers: Error"); return false; }

  JsonObject t = doc["data"]["timings"];
  out.fajr    = parseHHMMToMinutes((const char*)t["Fajr"]);
  out.sunrise = parseHHMMToMinutes((const char*)t["Sunrise"]);
  out.dhuhr   = parseHHMMToMinutes((const char*)t["Dhuhr"]);
  out.asr     = parseHHMMToMinutes((const char*)t["Asr"]);
  out.maghrib = parseHHMMToMinutes((const char*)t["Maghrib"]);
  out.isha    = parseHHMMToMinutes((const char*)t["Isha"]);

  if (!out.valid()) {
    LOGE(LOG_TAG_PRAY, "Parsed timings invalid (F=%d D=%d A=%d M=%d I=%d)",
         out.fajr, out.dhuhr, out.asr, out.maghrib, out.isha);
    updateSplashStatus("Prayers: Error"); return false;
  }

  LOGI(LOG_TAG_PRAY, "OK %s: F=%d D=%d A=%d M=%d I=%d",
       dateStr.c_str(), out.fajr, out.dhuhr, out.asr, out.maghrib, out.isha);
  return true;
}

bool fetchPrayerTimesTwoDays() {
  if (WiFi.status() != WL_CONNECTED) { updateSplashStatus("Prayers: Error"); return false; }

  bool locReady;
  xSemaphoreTake(g_dataMtx, portMAX_DELAY); locReady = g_locationReady; xSemaphoreGive(g_dataMtx);
  if (!locReady) { LOGW(LOG_TAG_PRAY, "No location yet"); updateSplashStatus("Prayers: Error"); return false; }

  time_t now = time(nullptr);
  String today = dateDDMMYYYY(now);
  String tomorrow = dateDDMMYYYY(now + 86400);

  PrayerTimesDay t0, t1;
  bool ok0 = fetchPrayerForDate(today, t0);
  bool ok1 = ok0 ? fetchPrayerForDate(tomorrow, t1) : false;

  if (!ok0 || !ok1) {
    LOGW(LOG_TAG_PRAY, "Fetch failed today=%d tomorrow=%d", ok0?1:0, ok1?1:0);
    updateSplashStatus("Prayers: Error"); return false;
  }

  // Apply per-prayer offsets (clamped to day range 0-1439)
  auto applyOffsets = [](PrayerTimesDay& d) {
    auto clamp = [](int v) -> int16_t { return (int16_t)(v < 0 ? 0 : v > 1439 ? 1439 : v); };
    d.fajr    = clamp((int)d.fajr    + (int)g_prayerOffsets[0]);
    d.dhuhr   = clamp((int)d.dhuhr   + (int)g_prayerOffsets[1]);
    d.asr     = clamp((int)d.asr     + (int)g_prayerOffsets[2]);
    d.maghrib = clamp((int)d.maghrib + (int)g_prayerOffsets[3]);
    d.isha    = clamp((int)d.isha    + (int)g_prayerOffsets[4]);
  };
  applyOffsets(t0);
  applyOffsets(t1);

  xSemaphoreTake(g_dataMtx, portMAX_DELAY);
  g_today = t0; g_tomorrow = t1; g_prayerReady = true; g_prayerRefetch = false;
  xSemaphoreGive(g_dataMtx);

  LOGI(LOG_TAG_PRAY, "Prayer times ready (2 days)");
  updateSplashStatus("Prayers: Ready");
  sendUi(UI_EVT_PRAYER_READY);
  return true;
}

// ============================================================================
// NTP sync
// ============================================================================
bool syncTimeNtp(uint32_t timeoutMs) {
  g_timeSynced = false;
  if (!g_tzConfigured) { LOGW(LOG_TAG_NTP, "TZ not configured"); updateSplashStatus("Time: Error"); return false; }

  const char* tz = getenv("TZ");
  if (!tz || !tz[0]) tz = "UTC0";
  updateSplashStatus("Syncing time...");
  configTzTime(tz, "pool.ntp.org", "time.nist.gov", "time.google.com");

  const uint32_t start = millis();
  struct tm tmNow;
  while ((millis() - start) < timeoutMs) {
    if (getLocalTime(&tmNow, 500)) {
      g_timeSynced = true;
      LOGI(LOG_TAG_NTP, "Time synced: %04d-%02d-%02d %02d:%02d:%02d TZ=%s",
           tmNow.tm_year+1900, tmNow.tm_mon+1, tmNow.tm_mday,
           tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec, tz);
      updateSplashStatus("Time: Synced");
      sendUi(UI_EVT_TIME_SYNCED);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  LOGW(LOG_TAG_NTP, "Time sync timeout (TZ=%s)", tz);
  updateSplashStatus("Sync: Error");
  return false;
}

// ============================================================================
// Smart calculation (AD-49)
// ============================================================================
// Mosque times of the fit just applied, kept until the Aladhan fetch confirms them.
static int16_t s_confirmFajr = -1, s_confirmAsr = -1;

static int signedDiff(int a, int b) {
  int d = (a - b) % 1440;
  if (d < -720) d += 1440;
  if (d >= 720) d -= 1440;
  return d;
}

bool smartFitToday(int mosqueFajr, int mosqueAsr, PcFit& out) {
  bool locReady;
  PcDay d;
  xSemaphoreTake(g_dataMtx, portMAX_DELAY);
  locReady = g_locationReady; d.lat = g_lat; d.lon = g_lon;
  xSemaphoreGive(g_dataMtx);
  if (!locReady || !g_timeSynced || !g_tzConfigured) return false;

  // Today's local date and its UTC offset from the configured POSIX TZ.
  const time_t now = time(nullptr);
  struct tm lt, gt;
  localtime_r(&now, &lt);
  gmtime_r(&now, &gt);
  int offMin = (lt.tm_hour - gt.tm_hour) * 60 + (lt.tm_min - gt.tm_min);
  if (lt.tm_year != gt.tm_year) offMin += (lt.tm_year > gt.tm_year) ? 1440 : -1440;
  else offMin += (lt.tm_yday - gt.tm_yday) * 1440;
  d.year = lt.tm_year + 1900; d.month = lt.tm_mon + 1; d.day = lt.tm_mday;
  d.tzHours = offMin / 60.0;

  const uint32_t t0 = micros();
  const bool ok = pcBestFit(d, mosqueFajr, mosqueAsr, g_method, g_school, g_latAdj, out);
  LOGI(LOG_TAG_PRAY, "Smart fit F=%d A=%d -> ok=%d m=%d s=%d l=%d dF=%d dA=%d (%lu us)",
       mosqueFajr, mosqueAsr, ok ? 1 : 0, out.method, out.school, out.latAdj,
       out.dFajr, out.dAsr, (unsigned long)(micros() - t0));
  return ok;
}

String smartFitSummary(const PcFit& fit) {
  char buf[120];
  const char* m = (fit.method >= 0 && fit.method < 24 && kMethodNames[fit.method][0]) ? kMethodNames[fit.method] : "?";
  snprintf(buf, sizeof(buf), "%s / %s / %s", m,
           kSchoolNames[fit.school == 1 ? 1 : 0], kLatAdjNames[(fit.latAdj >= 0 && fit.latAdj <= 3) ? fit.latAdj : 3]);
  String s = buf;
  if (fit.fajr >= 0) { snprintf(buf, sizeof(buf), " | Fajr %+d min", fit.dFajr); s += buf; }
  if (fit.asr >= 0)  { snprintf(buf, sizeof(buf), " | Asr %+d min", fit.dAsr); s += buf; }
  return s;
}

void applySmartFit(const PcFit& fit, int mosqueFajr, int mosqueAsr) {
  const bool changed = fit.method != g_method || fit.school != g_school || fit.latAdj != g_latAdj;
  g_method = fit.method; g_school = fit.school; g_latAdj = fit.latAdj;
  g_smartLast = smartFitSummary(fit);
  g_smartFajr = -1; g_smartAsr = -1;
  s_confirmFajr = mosqueFajr; s_confirmAsr = mosqueAsr;
  if (changed) g_prayerRefetch = true;   // keep the old times until the new ones arrive
  saveSettings();
  g_settingsDirty = false;
}

void runPendingSmartFit() {
  if (g_smartFajr < 0 && g_smartAsr < 0) return;

  PcFit fit;
  if (!smartFitToday(g_smartFajr, g_smartAsr, fit)) {
    LOGW(LOG_TAG_PRAY, "Smart fit waiting for location/time");
    return;  // keep it pending for the next session
  }

  if (fit.score > PC_CLOSE_MATCH_MAX_MIN) {
    // Too far off to apply unattended: keep the current settings.
    g_smartLast = "No close match, settings unchanged. Best: " + smartFitSummary(fit);
    g_smartFajr = -1; g_smartAsr = -1;
    saveSettings();
    g_settingsDirty = false;
    updateSplashStatus("Smart: no match");
    return;
  }
  applySmartFit(fit, g_smartFajr, g_smartAsr);
  updateSplashStatus("Smart: applied");
}

void confirmSmartFit() {
  if (s_confirmFajr < 0 && s_confirmAsr < 0) return;
  PrayerTimesDay t;
  bool ready;
  xSemaphoreTake(g_dataMtx, portMAX_DELAY);
  ready = g_prayerReady && !g_prayerRefetch; t = g_today;
  xSemaphoreGive(g_dataMtx);
  if (!ready) return;

  char buf[64];
  String s = " | Aladhan:";
  if (s_confirmFajr >= 0) { snprintf(buf, sizeof(buf), " Fajr %+d", signedDiff(t.fajr, s_confirmFajr)); s += buf; }
  if (s_confirmAsr >= 0)  { snprintf(buf, sizeof(buf), " Asr %+d", signedDiff(t.asr, s_confirmAsr)); s += buf; }
  g_smartLast += s;
  LOGI(LOG_TAG_PRAY, "Smart fit confirmed: %s", g_smartLast.c_str());
  s_confirmFajr = s_confirmAsr = -1;
  saveSettings();
}

// ============================================================================
// Next prayer computation
// ============================================================================
void computeNextPrayer(PrayerId& outId, int& outMin, bool& outFromTomorrow) {
  outId = PR_NONE; outMin = -1; outFromTomorrow = false;
  if (!g_prayerReady) return;

  PrayerTimesDay td, tm;
  xSemaphoreTake(g_dataMtx, portMAX_DELAY); td = g_today; tm = g_tomorrow; xSemaphoreGive(g_dataMtx);
  if (!td.valid() || !tm.valid()) return;

  struct tm t;
  if (!getLocalTime(&t, 0)) return;
  int nowMin = t.tm_hour * 60 + t.tm_min;

  if      (nowMin < td.fajr)    { outId = PR_FAJR;    outMin = td.fajr; }
  else if (nowMin < td.dhuhr)   { outId = PR_DHUHR;   outMin = td.dhuhr; }
  else if (nowMin < td.asr)     { outId = PR_ASR;     outMin = td.asr; }
  else if (nowMin < td.maghrib) { outId = PR_MAGHRIB; outMin = td.maghrib; }
  else if (nowMin < td.isha)    { outId = PR_ISHA;    outMin = td.isha; }
  else {
    outId = PR_FAJR; outMin = tm.fajr; outFromTomorrow = true;
  }
}

bool getNextPrayerCountdown(char* out, size_t outLen) {
  out[0] = '\0';
  if (!g_prayerReady || !g_today.valid()) return false;

  struct tm now;
  if (!getLocalTime(&now, 0)) return false;

  int currentTotalSec = (now.tm_hour * 60 + now.tm_min) * 60 + now.tm_sec;

  PrayerId nextId; int nextMin; bool fromTomorrow;
  computeNextPrayer(nextId, nextMin, fromTomorrow);
  if (nextId == PR_NONE) return false;

  int secondsUntil = nextMin * 60 - currentTotalSec;
  if (secondsUntil < 0) secondsUntil += 24 * 60 * 60;

  int hours   = secondsUntil / 3600;
  int minutes = (secondsUntil % 3600) / 60;

  if (hours > 0) snprintf(out, outLen, "%s in %dh %dm", PRAYER_NAME[(uint8_t)nextId], hours, minutes);
  else           snprintf(out, outLen, "%s in %dm",      PRAYER_NAME[(uint8_t)nextId], minutes);
  return true;
}

// ============================================================================
// Qibla direction
// ============================================================================
static float wrap360(float deg) {
  while (deg < 0)      deg += 360.0f;
  while (deg >= 360.0f) deg -= 360.0f;
  return deg;
}

bool computeQiblaBearingDeg(float& outDeg) {
  if (!g_locationReady) return false;

  double lat, lon;
  xSemaphoreTake(g_dataMtx, portMAX_DELAY); lat = g_lat; lon = g_lon; xSemaphoreGive(g_dataMtx);

  const double kaabaLat = 21.422487, kaabaLon = 39.826206;
  const double deg2rad = 3.14159265358979323846 / 180.0;
  const double rad2deg = 180.0 / 3.14159265358979323846;

  double phi1 = lat * deg2rad, phi2 = kaabaLat * deg2rad;
  double dLon = (kaabaLon - lon) * deg2rad;

  double y = sin(dLon) * cos(phi2);
  double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dLon);

  double brng = atan2(y, x) * rad2deg;
  outDeg = wrap360((float)brng);
  return true;
}

const char* bearingToCompass16(float deg) {
  static const char* dir16[] = {
    "N","NNE","NE","ENE","E","ESE","SE","SSE",
    "S","SSW","SW","WSW","W","WNW","NW","NNW"
  };
  deg = wrap360(deg);
  int idx = (int)((deg + 11.25f) / 22.5f);
  idx &= 15;
  return dir16[idx];
}