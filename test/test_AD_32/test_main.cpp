/*
 * test_AD_32 — NVS location cache
 *
 * Stubs all hardware. Tests logic extracted from PrayerEngine.cpp:
 *   - saveCachedLocation / tryLoadCachedLocation behaviour
 *   - Cache used on fetch failure (AC-2)
 *   - Cache miss returns false (AC-4)
 *   - Factory reset clears cache (AC-5)
 *   - Successful fetch overwrites cache (AC-6)
 */
#include <unity.h>
#include <string>
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// Minimal stubs
// ---------------------------------------------------------------------------
static double  s_lat  = 999.0;
static double  s_lon  = 999.0;
static char    s_tz[64] = "";
static bool    s_cleared = false;

// Preferences stub
struct Preferences {
  bool   _ro = false;
  bool begin(const char*, bool ro = false) { _ro = ro; return true; }
  void end() {}
  void putDouble(const char* k, double v) {
    if (strcmp(k, "cLat") == 0) s_lat = v;
    else if (strcmp(k, "cLon") == 0) s_lon = v;
  }
  void putString(const char* k, const std::string& v) {
    if (strcmp(k, "cTz") == 0) strncpy(s_tz, v.c_str(), sizeof(s_tz)-1);
  }
  double getDouble(const char* k, double def) const {
    if (strcmp(k, "cLat") == 0) return s_lat;
    if (strcmp(k, "cLon") == 0) return s_lon;
    return def;
  }
  std::string getString(const char* k, const char* def) const {
    if (strcmp(k, "cTz") == 0) return strlen(s_tz) ? s_tz : def;
    return def;
  }
  void clear() { s_lat = 999.0; s_lon = 999.0; s_tz[0] = '\0'; s_cleared = true; }
};

// String stub
struct String : public std::string {
  String() = default;
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
  bool isEmpty() const { return empty(); }
  size_t length() const { return std::string::length(); }
  const char* c_str() const { return std::string::c_str(); }
};

// Globals stubs
static double g_lat = 0, g_lon = 0;
static String g_tzIana;
static bool   g_locationReady = false;
static String g_tzOverride;

struct SemaphoreHandle_t_stub {};
static SemaphoreHandle_t_stub* g_dataMtx = nullptr;
#define xSemaphoreTake(m, t) (void)0
#define xSemaphoreGive(m)    (void)0
#define portMAX_DELAY        0xFFFFFFFF

static char s_splashStatus[64] = "";
void updateSplashStatus(const char* s) { strncpy(s_splashStatus, s, sizeof(s_splashStatus)-1); }
void applyTimezonePosix(const String&) {}

enum UiEventType : uint8_t { UI_EVT_LOCATION_READY = 10 };
static UiEventType s_lastUiEvent = (UiEventType)0xFF;
void sendUi(UiEventType t) { s_lastUiEvent = t; }

#define LOG_TAG_LOC "LOC"
#define LOGI(tag, fmt, ...) (void)0
#define LOGE(tag, fmt, ...) (void)0

// ---------------------------------------------------------------------------
// Inline implementation under test (mirrors PrayerEngine.cpp logic)
// ---------------------------------------------------------------------------
static const char* KEY_C_LAT = "cLat";
static const char* KEY_C_LON = "cLon";
static const char* KEY_C_TZ  = "cTz";

static void saveCachedLocation(double lat, double lon, const String& tz) {
  Preferences prefs;
  if (!prefs.begin("settings", false)) return;
  prefs.putDouble(KEY_C_LAT, lat);
  prefs.putDouble(KEY_C_LON, lon);
  prefs.putString(KEY_C_TZ,  tz);
  prefs.end();
}

static bool tryLoadCachedLocation() {
  Preferences prefs;
  if (!prefs.begin("settings", true)) return false;
  const double lat = prefs.getDouble(KEY_C_LAT, 999.0);
  const double lon = prefs.getDouble(KEY_C_LON, 999.0);
  const String tz  = prefs.getString(KEY_C_TZ,  "").c_str();
  prefs.end();

  if (lat > 180.0 || tz.isEmpty()) return false;

  LOGI(LOG_TAG_LOC, "Using cached location lat=%.6f lon=%.6f tz=%s", lat, lon, tz.c_str());

  xSemaphoreTake(g_dataMtx, portMAX_DELAY);
  g_lat = lat; g_lon = lon; g_tzIana = tz; g_locationReady = true;
  xSemaphoreGive(g_dataMtx);

  applyTimezonePosix(tz);
  sendUi(UI_EVT_LOCATION_READY);
  updateSplashStatus("Location Ready");
  return true;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void resetNvs() {
  s_lat = 999.0; s_lon = 999.0; s_tz[0] = '\0'; s_cleared = false;
}
static void resetGlobals() {
  g_lat = 0; g_lon = 0; g_tzIana = ""; g_locationReady = false;
  s_splashStatus[0] = '\0'; s_lastUiEvent = (UiEventType)0xFF;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// AC-1 / AC-6: successful fetch saves to NVS
void test_save_persists_lat_lon_tz() {
  resetNvs();
  saveCachedLocation(51.5164, -0.093, "Europe/London");
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, 51.5164, s_lat);
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, -0.093,  s_lon);
  TEST_ASSERT_EQUAL_STRING("Europe/London", s_tz);
}

// AC-2: cache loaded when fetch fails
void test_cache_loaded_on_fetch_failure() {
  resetNvs(); resetGlobals();
  saveCachedLocation(51.5164, -0.093, "Europe/London");
  const bool ok = tryLoadCachedLocation();
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, 51.5164, g_lat);
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, -0.093,  g_lon);
  TEST_ASSERT_EQUAL_STRING("Europe/London", g_tzIana.c_str());
  TEST_ASSERT_TRUE(g_locationReady);
}

// AC-2: splash shows "Location Ready" on cache hit
void test_cache_hit_sets_splash_ready() {
  resetNvs(); resetGlobals();
  saveCachedLocation(51.5164, -0.093, "Europe/London");
  tryLoadCachedLocation();
  TEST_ASSERT_EQUAL_STRING("Location Ready", s_splashStatus);
}

// AC-2: UI event fired on cache hit
void test_cache_hit_sends_ui_event() {
  resetNvs(); resetGlobals();
  saveCachedLocation(51.5164, -0.093, "Europe/London");
  tryLoadCachedLocation();
  TEST_ASSERT_EQUAL_UINT8(UI_EVT_LOCATION_READY, s_lastUiEvent);
}

// AC-4: no cache → returns false (first boot, never fetched)
void test_cache_miss_returns_false() {
  resetNvs(); resetGlobals();
  // NVS has sentinel values — nothing was ever saved
  const bool ok = tryLoadCachedLocation();
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_FALSE(g_locationReady);
}

// AC-4: empty tz string treated as cache miss
void test_cache_miss_empty_tz_returns_false() {
  resetNvs(); resetGlobals();
  s_lat = 51.5164; s_lon = -0.093; // lat/lon present but no tz
  const bool ok = tryLoadCachedLocation();
  TEST_ASSERT_FALSE(ok);
}

// AC-5: factory reset (prefs.clear()) wipes the cache
void test_factory_reset_clears_cache() {
  resetNvs(); resetGlobals();
  saveCachedLocation(51.5164, -0.093, "Europe/London");
  // Simulate factory reset
  Preferences prefs;
  prefs.begin("settings", false);
  prefs.clear();
  prefs.end();
  TEST_ASSERT_TRUE(s_cleared);
  // After clear, load should fail
  const bool ok = tryLoadCachedLocation();
  TEST_ASSERT_FALSE(ok);
}

// AC-6: second successful fetch overwrites existing cache
void test_save_overwrites_existing_cache() {
  resetNvs();
  saveCachedLocation(51.5164, -0.093,  "Europe/London");
  saveCachedLocation(40.7128, -74.006, "America/New_York");
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, 40.7128, s_lat);
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, -74.006, s_lon);
  TEST_ASSERT_EQUAL_STRING("America/New_York", s_tz);
}

// Edge: lat exactly on boundary (180.0) is valid
void test_cache_lat_at_boundary_valid() {
  resetNvs(); resetGlobals();
  saveCachedLocation(180.0, 0.0, "Pacific/Auckland");
  const bool ok = tryLoadCachedLocation();
  TEST_ASSERT_TRUE(ok);
}

// Edge: southern hemisphere negative lat stored and restored correctly
void test_cache_southern_hemisphere() {
  resetNvs(); resetGlobals();
  saveCachedLocation(-33.8688, 151.2093, "Australia/Sydney");
  tryLoadCachedLocation();
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, -33.8688, g_lat);
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, 151.2093, g_lon);
  TEST_ASSERT_EQUAL_STRING("Australia/Sydney", g_tzIana.c_str());
}

// ---------------------------------------------------------------------------
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_save_persists_lat_lon_tz);
  RUN_TEST(test_cache_loaded_on_fetch_failure);
  RUN_TEST(test_cache_hit_sets_splash_ready);
  RUN_TEST(test_cache_hit_sends_ui_event);
  RUN_TEST(test_cache_miss_returns_false);
  RUN_TEST(test_cache_miss_empty_tz_returns_false);
  RUN_TEST(test_factory_reset_clears_cache);
  RUN_TEST(test_save_overwrites_existing_cache);
  RUN_TEST(test_cache_lat_at_boundary_valid);
  RUN_TEST(test_cache_southern_hemisphere);
  return UNITY_END();
}
