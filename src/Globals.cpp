/*
 * Globals.cpp — Definitions for shared globals declared in Globals.h.
 */
#include "Globals.h"

// ============================================================================
// Version
// ============================================================================
const char* FW_VER = "0.3.08";
const char* HW_REV = "Rev-A";

// ============================================================================
// Prayer names
// ============================================================================
const char* PRAYER_NAME[] = { "-", "FAJR", "DHUHR", "ASR", "MAGHRIB", "ISHA" };

// ============================================================================
// Prayer setting name tables
// ============================================================================
const char* kMethodNames[24] = {
  "Jafari","Karachi","ISNA","MWL","Makkah","Egypt",
  "", "Tehran","Gulf","Kuwait","Qatar","Singapore",
  "France","Turkey","Russia","Moonsighting","Dubai",
  "Malaysia","Tunisia","Algeria","Indonesia","Morocco",
  "Lisbon","Jordan"
};

const char* kSchoolNames[2] = { "Shafi", "Hanafi" };

const char* kLatAdjNames[4] = {
  "None", "Middle of Night", "1/7 Night", "Angle Based"
};

// ============================================================================
// Timezone lookup table (replaces std::map)
// ============================================================================
const TzEntry IANA_TO_POSIX[] = {
  { "Europe/London",       "GMT0BST,M3.5.0/1,M10.5.0/2" },
  { "Europe/Copenhagen",   "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "Europe/Oslo",         "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "Europe/Stockholm",    "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "Europe/Paris",        "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "Europe/Madrid",       "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "Europe/Rome",         "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "Europe/Berlin",       "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "Europe/Helsinki",     "EET-2EEST,M3.5.0/3,M10.5.0/4" },
  { "America/New_York",    "EST5EDT,M3.2.0/2,M11.1.0/2" },
  { "America/Chicago",     "CST6CDT,M3.2.0/2,M11.1.0/2" },
  { "America/Denver",      "MST7MDT,M3.2.0/2,M11.1.0/2" },
  { "America/Los_Angeles", "PST8PDT,M3.2.0/2,M11.1.0/2" },
  { "Australia/Sydney",    "AEST-10AEDT,M10.1.0/2,M4.1.0/3" },
  { "Pacific/Auckland",    "NZST-12NZDT,M9.5.0/2,M4.1.0/3" }
};
const int IANA_TO_POSIX_COUNT = sizeof(IANA_TO_POSIX) / sizeof(IANA_TO_POSIX[0]);

const char* findPosix(const String& iana) {
  for (int i = 0; i < IANA_TO_POSIX_COUNT; i++) {
    if (iana == IANA_TO_POSIX[i].iana) return IANA_TO_POSIX[i].posix;
  }
  return nullptr;
}

// ============================================================================
// RTOS Handles
// ============================================================================
QueueHandle_t     rawBtnQueue  = nullptr;
QueueHandle_t     uiQueue      = nullptr;
QueueHandle_t     wifiCmdQueue = nullptr;

TimerHandle_t     splashTimer    = nullptr;
TimerHandle_t     tickTimer      = nullptr;
TimerHandle_t     ledPulseTimer  = nullptr;

SemaphoreHandle_t g_displayMtx = nullptr;
SemaphoreHandle_t g_logMtx     = nullptr;
SemaphoreHandle_t g_dataMtx    = nullptr;
SemaphoreHandle_t g_fotaMtx    = nullptr;
SemaphoreHandle_t g_splashMtx  = nullptr;

// ============================================================================
// Display
// ============================================================================
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ============================================================================
// Settings values (defaults)
// ============================================================================
uint8_t  g_timeFormat = 1;       // default 24Hr
uint8_t  g_adhanType  = 0;       // default Full
bool     g_duaEnabled = true;
String   g_tzOverride = "";       // "" = Auto
int      g_method     = 2;       // ISNA
int      g_school     = 1;       // Hanafi
int      g_latAdj     = 3;       // Angle Based
bool     g_countdownBannerEnabled = true;

bool     g_settingsDirty = false;

// ============================================================================
// Volume
// ============================================================================
volatile uint8_t g_currentVolIdx = 1;
const int        VOL_LEVELS[4] = { 0, 7, 14, 21 };
const char*      VOL_NAMES[4]  = { "MUTE", "LOW", "MED", "HIGH" };
uint8_t          g_prevVolIdx   = 1;

// ============================================================================
// Temp Mute
// ============================================================================
volatile bool     g_tempMuteActive      = false;
volatile uint8_t  g_tempMutePrevVolIdx  = 1;
volatile uint32_t g_tempMuteStartedMs   = 0;
const uint32_t    TEMP_MUTE_MAX_DURATION_MS = 5 * 60 * 1000;

// ============================================================================
// DFPlayer state
// ============================================================================
volatile bool     g_dfPowered    = false;
bool              g_dfOk         = false;
volatile uint8_t  g_dfBusyLevel  = 1;
volatile bool     g_dfIsPlaying  = false;
volatile bool     g_dfStarting   = false;
volatile uint32_t g_dfPlayCmdAtMs = 0;
volatile PrayerId g_playbackPrayerId = PR_NONE;

// ============================================================================
// Location / Prayer
// ============================================================================
volatile bool  g_locationReady = false;
volatile bool  g_prayerReady   = false;
double         g_lat  = 0.0;
double         g_lon  = 0.0;
String         g_tzIana = "UTC";
PrayerTimesDay g_today;
PrayerTimesDay g_tomorrow;

// ============================================================================
// Time
// ============================================================================
bool     g_timeSynced    = false;
volatile bool g_tzConfigured = false;

// ============================================================================
// FOTA
// ============================================================================
String   g_fotaLatestCached     = "";
bool     g_fotaUpdateAvailable  = false;
uint32_t g_fotaLastCheckDayKey  = 0;
volatile bool g_fotaBusy        = false;
volatile bool g_idleUpdateBanner = false;

// ============================================================================
// OLED state
// ============================================================================
volatile bool     g_oledIsOn       = true;
volatile uint32_t g_oledOnUntilMs  = 0;

// ============================================================================
// UI flow
// ============================================================================
bool     forceIdleRedraw     = false;
volatile bool g_wifiConnectFailed  = false;
volatile bool g_restartAfterPortal = false;
const char* g_infoTitle = nullptr;

// ============================================================================
// Factory Reset
// ============================================================================
volatile bool g_factoryResetActive = false;
volatile uint32_t g_factoryResetStartMs = 0;
const uint32_t FACTORY_RESET_COUNTDOWN_MS = 5000;  // 5 second countdown after 10s hold

// ============================================================================
// Splash
// ============================================================================
String   g_splashStatus = "";
volatile bool g_splashStatusChanged = false;

// ============================================================================
// Preferences keys (namespace/key strings)
// ============================================================================
static const char* PREF_NS_WIFI     = "adhan_ai";
static const char* KEY_SSID         = "ssid";
static const char* KEY_PASS         = "pass";

static const char* PREF_NS_SETTINGS = "settings";
static const char* KEY_TIME_FMT     = "timeFmt";
static const char* KEY_P_METHOD     = "pMethod";
static const char* KEY_P_SCHOOL     = "pSchool";
static const char* KEY_P_LATADJ     = "pLatAdj";
static const char* KEY_ADHAN_TYPE   = "adhanType";
static const char* KEY_DUA          = "dua";
static const char* KEY_VOL_IDX      = "volIdx";
static const char* KEY_TZ_OVERRIDE  = "tz";
static const char* KEY_FOTA_LATEST  = "fotaLatest";
static const char* KEY_FOTA_AVAIL   = "fotaAvail";
static const char* KEY_FOTA_LASTDAY = "fotaLastDay";
static const char* KEY_COUNTDOWN_BANNER = "cntBanner";

static Preferences prefs;

// ============================================================================
// sendUi
// ============================================================================
void sendUi(UiEventType t) {
  UiEvent ev{ t };
  const bool isButton = (t == UI_EVT_BTN_UP || t == UI_EVT_BTN_DN ||
                          t == UI_EVT_BTN_OK || t == UI_EVT_BTN_OK_LONG);
  BaseType_t ok = xQueueSend(uiQueue, &ev, 0);
  if (ok == pdTRUE) return;

  if (isButton) {
    UiEvent dummy;
    xQueueReceive(uiQueue, &dummy, 0);
    ok = xQueueSend(uiQueue, &ev, 0);
    if (ok != pdTRUE) {
      LOGW(LOG_TAG_UI, "Failed to queue button event after making room");
    }
  }
}

// ============================================================================
// OLED power helpers
// ============================================================================
void oledSetPower(bool on) {
  if (g_displayMtx) xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  if (on) display.ssd1306_command(0xAF);
  else    display.ssd1306_command(0xAE);
  if (g_displayMtx) xSemaphoreGive(g_displayMtx);

  g_oledIsOn = on;
  if (!on) {
    ledsAllOff();
  }
}

void oledSetContrastSafe(uint8_t c) {
  if (!g_oledIsOn || !g_displayMtx) return;
  xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(c);
  xSemaphoreGive(g_displayMtx);
}

void oledWakeFor(uint32_t ms) {
  uint32_t now   = millis();
  uint32_t until = now + ms;
  if ((int32_t)(until - g_oledOnUntilMs) > 0) g_oledOnUntilMs = until;

  if (!g_oledIsOn) {
    oledSetPower(true);
    forceIdleRedraw = true;
  }
}

// ============================================================================
// Splash status
// ============================================================================
void updateSplashStatus(const char* status) {
  if (g_splashMtx) xSemaphoreTake(g_splashMtx, portMAX_DELAY);
  g_splashStatus = status;
  g_splashStatusChanged = true;
  if (g_splashMtx) xSemaphoreGive(g_splashMtx);
  LOGI(LOG_TAG_SYS, "Splash: %s", status);
}

// ============================================================================
// Settings persistence
// ============================================================================
void loadSettings() {
  prefs.begin(PREF_NS_SETTINGS, true);
  g_timeFormat     = prefs.getUChar(KEY_TIME_FMT, 1);
  g_adhanType      = prefs.getUChar(KEY_ADHAN_TYPE, 0);
  g_duaEnabled     = prefs.getBool(KEY_DUA, true);
  g_currentVolIdx  = prefs.getUChar(KEY_VOL_IDX, 1);
  g_method = prefs.getInt(KEY_P_METHOD, g_method);
  g_school = prefs.getInt(KEY_P_SCHOOL, g_school);
  g_latAdj = prefs.getInt(KEY_P_LATADJ, g_latAdj);
  g_tzOverride = prefs.getString(KEY_TZ_OVERRIDE, "");
  g_fotaLatestCached   = prefs.getString(KEY_FOTA_LATEST, "");
  g_fotaUpdateAvailable = prefs.getBool(KEY_FOTA_AVAIL, false);
  g_fotaLastCheckDayKey = prefs.getUInt(KEY_FOTA_LASTDAY, 0);
  g_idleUpdateBanner = g_fotaUpdateAvailable;
  g_countdownBannerEnabled = prefs.getBool(KEY_COUNTDOWN_BANNER, true);
  prefs.end();

  if (g_timeFormat > 1) g_timeFormat = 1;
  if (g_currentVolIdx > 3) g_currentVolIdx = 1;
  if (g_adhanType > 2)  g_adhanType = 0;
  if (g_method < 0 || g_method > 23) g_method = 2;
  if (g_school < 0 || g_school > 1)  g_school = 1;
  if (g_latAdj < 0 || g_latAdj > 3)  g_latAdj = 3;
}

void saveSettings() {
  prefs.begin(PREF_NS_SETTINGS, false);
  prefs.putUChar(KEY_TIME_FMT, g_timeFormat);
  prefs.putUChar(KEY_ADHAN_TYPE, g_adhanType);
  prefs.putBool(KEY_DUA, g_duaEnabled);
  prefs.putUChar(KEY_VOL_IDX, g_currentVolIdx);
  prefs.putInt(KEY_P_METHOD, g_method);
  prefs.putInt(KEY_P_SCHOOL, g_school);
  prefs.putInt(KEY_P_LATADJ, g_latAdj);
  prefs.putString(KEY_TZ_OVERRIDE, g_tzOverride);
  prefs.putString(KEY_FOTA_LATEST, g_fotaLatestCached);
  prefs.putBool(KEY_FOTA_AVAIL, g_fotaUpdateAvailable);
  prefs.putUInt(KEY_FOTA_LASTDAY, g_fotaLastCheckDayKey);
  prefs.putBool(KEY_COUNTDOWN_BANNER, g_countdownBannerEnabled);
  prefs.end();
}

void flushSettingsIfDirty() {
  if (!g_settingsDirty) return;
  saveSettings();
  g_settingsDirty = false;
}

// WiFi cred helpers (used only by WifiManager, but defined here for Preferences access)
bool loadCreds(String& ssid, String& pass) {
  prefs.begin(PREF_NS_WIFI, true);
  ssid = prefs.getString(KEY_SSID, "");
  pass = prefs.getString(KEY_PASS, "");
  prefs.end();
  return ssid.length() > 0;
}

void saveCreds(const String& ssid, const String& pass) {
  prefs.begin(PREF_NS_WIFI, false);
  prefs.putString(KEY_SSID, ssid);
  prefs.putString(KEY_PASS, pass);
  prefs.end();
}

void eraseCreds() {
  prefs.begin(PREF_NS_WIFI, false);
  prefs.remove(KEY_SSID);
  prefs.remove(KEY_PASS);
  prefs.end();
  LOGW(LOG_TAG_WIFI, "WiFi creds erased");
}

// ============================================================================
// Time formatting utilities
// ============================================================================
int16_t parseHHMMToMinutes(const char* s) {
  if (!s) return -1;
  int h = -1, m = -1;
  if (isdigit((unsigned char)s[0]) && isdigit((unsigned char)s[1]) && s[2]==':' &&
      isdigit((unsigned char)s[3]) && isdigit((unsigned char)s[4])) {
    h = (s[0]-'0')*10 + (s[1]-'0');
    m = (s[3]-'0')*10 + (s[4]-'0');
  } else {
    return -1;
  }
  if (h<0 || h>23 || m<0 || m>59) return -1;
  return (int16_t)(h*60+m);
}

String dateDDMMYYYY(time_t t) {
  struct tm ti;
  localtime_r(&t, &ti);
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d-%02d-%04d", ti.tm_mday, ti.tm_mon+1, ti.tm_year+1900);
  return String(buf);
}

void fmtHHMM(int minutes, char out[6]) {
  if (minutes < 0) { strcpy(out, "--:--"); return; }
  int h = (minutes / 60) % 24;
  int m = minutes % 60;
  snprintf(out, 6, "%02d:%02d", h, m);
}

void fmtTimeNow(char out[12]) {
  struct tm tmNow;
  if (!getLocalTime(&tmNow, 0)) { strcpy(out, "--:--"); return; }
  int h24 = tmNow.tm_hour;
  int m   = tmNow.tm_min;

  if (g_timeFormat == 0) {
    int h12 = h24 % 12; if (h12 == 0) h12 = 12;
    const bool isPM = (h24 >= 12);
    snprintf(out, 12, "%02d:%02d %s", h12, m, isPM ? "PM" : "AM");
  } else {
    snprintf(out, 12, "%02d:%02d", h24, m);
  }
}

void fmtMinutesToClock(int minutes, char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  if (minutes < 0) { snprintf(out, outLen, "--:--"); return; }

  int h24 = (minutes / 60) % 24;
  int m   = minutes % 60;

  if (g_timeFormat == 0) {
    int h12 = h24 % 12;
    if (h12 == 0) h12 = 12;
    const bool isPM = (h24 >= 12);
    snprintf(out, outLen, "%02d:%02d%s", h12, m, isPM ? " PM" : "");
  } else {
    snprintf(out, outLen, "%02d:%02d", h24, m);
  }
}

void applyTimezonePosix(const String& iana) {
  const char* posix = findPosix(iana);
  if (posix) {
    setenv("TZ", posix, 1);
    tzset();
    configTzTime(posix, "pool.ntp.org", "time.nist.gov", "time.google.com");
    g_tzConfigured = true;
    LOGI(LOG_TAG_LOC, "TZ applied POSIX=%s (iana=%s)", posix, iana.c_str());
  } else {
    setenv("TZ", "UTC0", 1);
    tzset();
    configTzTime("UTC0", "pool.ntp.org", "time.nist.gov", "time.google.com");
    g_tzConfigured = false;
    LOGW(LOG_TAG_LOC, "No POSIX map for %s -> using UTC0", iana.c_str());
  }
}

uint32_t dayKeyNowLocal() {
  struct tm lt;
  if (!getLocalTime(&lt, 0)) return 0;
  return (uint32_t)(lt.tm_year + 1900) * 10000u +
         (uint32_t)(lt.tm_mon + 1) * 100u +
         (uint32_t)lt.tm_mday;
}

// ============================================================================
// Factory Reset
// ============================================================================
void performFactoryReset() {
  LOGI(LOG_TAG_SYS, "=== FACTORY RESET INITIATED ===");
  
  // Clear all settings
  prefs.begin(PREF_NS_SETTINGS, false);
  prefs.clear();
  prefs.end();
  LOGI(LOG_TAG_SYS, "Settings namespace cleared");
  
  // Clear WiFi credentials
  prefs.begin(PREF_NS_WIFI, false);
  prefs.clear();
  prefs.end();
  LOGI(LOG_TAG_SYS, "WiFi credentials cleared");
  
  // Show reset complete message
  if (g_displayMtx) xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  
  // Draw a box
  display.drawRect(10, 15, 108, 35, SSD1306_WHITE);
  
  // Center text
  display.setCursor(20, 25);
  display.print("Factory Reset");
  display.setCursor(30, 38);
  display.print("Complete!");
  
  display.display();
  if (g_displayMtx) xSemaphoreGive(g_displayMtx);
  
  LOGI(LOG_TAG_SYS, "Restarting in 2 seconds...");
  vTaskDelay(pdMS_TO_TICKS(2000));
  
  ESP.restart();
}

