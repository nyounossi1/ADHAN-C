/*
 * Globals.h — Shared types, enums, globals, and RTOS handles.
 * Every module includes this. Definitions live in Globals.cpp.
 *
 * Project: A:DHAN AI Adhan Clock
 */
#pragma once

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <WiFi.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

// ============================================================================
// Hardware Pin Definitions
// ============================================================================
#define BTN_UP    35
#define BTN_OK    32
#define BTN_DN    25

#define OK_LED_PIN   26
#define BTN_LED_PIN  27

#define SDA_PIN 21
#define SCL_PIN 22
#define OLED_ADDR 0x3C

static constexpr int DF_ON_PIN   = 13;
static constexpr int DF_RX_PIN   = 16;
static constexpr int DF_TX_PIN   = 17;
static constexpr int DF_BUSY_PIN = 4;

// ============================================================================
// Firmware / Hardware Version
// ============================================================================
extern const char* FW_VER;
extern const char* HW_REV;

// ============================================================================
// Build Flags
// ============================================================================
#define LOG_ENABLED 1
#define DEV_BUILD   1

// ============================================================================
// Logging
// ============================================================================
#define LOG_TAG_UI     "UI"
#define LOG_TAG_BTN    "BTN"
#define LOG_TAG_SYS    "SYS"
#define LOG_TAG_OLED   "OLED"
#define LOG_TAG_WIFI   "WIFI"
#define LOG_TAG_NTP    "NTP"
#define LOG_TAG_LOC    "LOC"
#define LOG_TAG_PRAY   "PRAY"
#define LOG_TAG_ADHAN  "ADHAN"
#define LOG_TAG_MENU   "MENU"

extern SemaphoreHandle_t g_logMtx;

#if LOG_ENABLED
  #define LOG_BASE(level, tag, fmt, ...) do {                                 \
    if (g_logMtx) xSemaphoreTake(g_logMtx, portMAX_DELAY);                    \
    Serial.printf("[%s][%s] " fmt "\n", level, tag, ##__VA_ARGS__);           \
    if (g_logMtx) xSemaphoreGive(g_logMtx);                                   \
  } while (0)
  #define LOGI(tag, fmt, ...) LOG_BASE("I", tag, fmt, ##__VA_ARGS__)
  #define LOGW(tag, fmt, ...) LOG_BASE("W", tag, fmt, ##__VA_ARGS__)
  #define LOGE(tag, fmt, ...) LOG_BASE("E", tag, fmt, ##__VA_ARGS__)
#else
  #define LOGI(...)
  #define LOGW(...)
  #define LOGE(...)
#endif

// ============================================================================
// Enums / Structs shared across modules
// ============================================================================

// --- Prayer ---
struct PrayerTimesDay {
  int16_t fajr    = -1;
  int16_t sunrise = -1;
  int16_t dhuhr   = -1;
  int16_t asr     = -1;
  int16_t maghrib = -1;
  int16_t isha    = -1;
  bool valid() const { return fajr>=0 && dhuhr>=0 && asr>=0 && maghrib>=0 && isha>=0; }
};

enum PrayerId : uint8_t { PR_NONE=0, PR_FAJR, PR_DHUHR, PR_ASR, PR_MAGHRIB, PR_ISHA };
extern const char* PRAYER_NAME[];

// --- UI Events ---
enum UiEventType : uint8_t {
  UI_EVT_BTN_UP,
  UI_EVT_BTN_DN,
  UI_EVT_BTN_OK,
  UI_EVT_BTN_OK_LONG,
  UI_EVT_SPLASH_DONE,
  UI_EVT_TICK,
  UI_EVT_WIFI_AP_STARTED,
  UI_EVT_WIFI_STA_CONNECTING,
  UI_EVT_WIFI_STA_CONNECTED,
  UI_EVT_TIME_SYNCED,
  UI_EVT_LOCATION_READY,
  UI_EVT_PRAYER_READY,
  UI_EVT_DF_BUSY_CHANGED,
  UI_EVT_FACTORY_RESET_TRIGGER,  // 10-second hold detected
  UI_EVT_FACTORY_RESET_CANCEL    // Button released during countdown
};

struct UiEvent { UiEventType type; };

enum RawBtnId : uint8_t { RAW_BTN_UP, RAW_BTN_DN, RAW_BTN_OK };
struct RawBtnEvent { RawBtnId id; uint32_t isrMs; };

// --- Screen State ---
enum ScreenState : uint8_t {
  SCREEN_SPLASH,
  SCREEN_WIFI_AP,
  SCREEN_ARC_IDLE,
  SCREEN_PRAYER_SCREEN,
  SCREEN_MENU_ENGINE,
  SCREEN_INFO,
  SCREEN_CONFIRM,
  SCREEN_WIFI_STATUS,
  SCREEN_LIST,
  SCREEN_FACTORY_RESET  // Factory reset countdown screen
};

// --- WiFi Commands ---
enum WifiCmdType : uint8_t {
  WIFI_CMD_START_AP_PORTAL,
  WIFI_CMD_FORGET_AND_START_AP,
  WIFI_CMD_RUN_DAILY_REFRESH,
  WIFI_CMD_RUN_FOTA_CHECK,
  WIFI_CMD_WIFI_STATUS_ONESHOT
};
struct WifiCmd { WifiCmdType type; };

// --- Info Mode (for INFO screen) ---
enum InfoMode : uint8_t {
  INFO_NONE = 0,
  INFO_TEXT = 1,
  INFO_FOTA_STATUS = 2,
  INFO_CURRENT_SETTINGS = 3,
  INFO_ABOUT = 4,
  INFO_QIBLA = 5
};

// --- Timezone lookup ---
struct TzEntry { const char* iana; const char* posix; };
extern const TzEntry IANA_TO_POSIX[];
extern const int     IANA_TO_POSIX_COUNT;
const char* findPosix(const String& iana);

// ============================================================================
// Prayer Setting Names (shared by portal + menu + settings pages)
// ============================================================================
extern const char* kMethodNames[24];
extern const char* kSchoolNames[2];
extern const char* kLatAdjNames[4];

// ============================================================================
// RTOS Handles (created in main, used across modules)
// ============================================================================
extern QueueHandle_t     rawBtnQueue;
extern QueueHandle_t     uiQueue;
extern QueueHandle_t     wifiCmdQueue;

extern TimerHandle_t     splashTimer;
extern TimerHandle_t     tickTimer;
extern TimerHandle_t     ledPulseTimer;

extern SemaphoreHandle_t g_displayMtx;
extern SemaphoreHandle_t g_dataMtx;
extern SemaphoreHandle_t g_fotaMtx;
extern SemaphoreHandle_t g_splashMtx;

// ============================================================================
// Display (global — created in main.cpp, used by UI modules)
// ============================================================================
extern Adafruit_SSD1306 display;

// ============================================================================
// Shared Globals (definitions in Globals.cpp)
// ============================================================================

// --- Settings ---
extern uint8_t  g_timeFormat;      // 0=12Hr, 1=24Hr
extern uint8_t  g_adhanType;       // 0=Full, 1=Short, 2=Chime
extern bool     g_duaEnabled;
extern String   g_tzOverride;      // "" = Auto
extern int      g_method;
extern int      g_school;
extern int      g_latAdj;
extern bool     g_countdownBannerEnabled;

// --- Volume ---
extern volatile uint8_t g_currentVolIdx;
extern const int        VOL_LEVELS[4];
extern const char*      VOL_NAMES[4];
extern uint8_t          g_prevVolIdx;

// --- Temp Mute ---
extern volatile bool     g_tempMuteActive;
extern volatile uint8_t  g_tempMutePrevVolIdx;
extern volatile uint32_t g_tempMuteStartedMs;
extern const uint32_t    TEMP_MUTE_MAX_DURATION_MS;

// --- DFPlayer state ---
extern volatile bool     g_dfPowered;
extern bool              g_dfOk;
extern volatile uint8_t  g_dfBusyLevel;
extern volatile bool     g_dfIsPlaying;
extern volatile bool     g_dfStarting;
extern volatile uint32_t g_dfPlayCmdAtMs;
extern volatile PrayerId g_playbackPrayerId;

// --- Location / Prayer ---
extern volatile bool  g_locationReady;
extern volatile bool  g_prayerReady;
extern double         g_lat;
extern double         g_lon;
extern String         g_tzIana;
extern PrayerTimesDay g_today;
extern PrayerTimesDay g_tomorrow;

// --- Time ---
extern bool     g_timeSynced;
extern volatile bool g_tzConfigured;

// --- FOTA ---
extern String   g_fotaLatestCached;
extern bool     g_fotaUpdateAvailable;
extern uint32_t g_fotaLastCheckDayKey;
extern volatile bool g_fotaBusy;
extern String g_fotaStatus;
extern volatile bool g_idleUpdateBanner;

// --- OLED state ---
extern volatile bool     g_oledIsOn;
extern volatile uint32_t g_oledOnUntilMs;

// --- UI flow ---
extern bool     forceIdleRedraw;
extern bool     g_settingsDirty;
extern volatile bool g_wifiConnectFailed;
extern volatile bool g_restartAfterPortal;
extern const char* g_infoTitle;

// --- Factory Reset ---
extern volatile bool g_factoryResetActive;
extern volatile uint32_t g_factoryResetStartMs;
extern const uint32_t FACTORY_RESET_COUNTDOWN_MS;

// --- Splash ---
extern String   g_splashStatus;
extern volatile bool g_splashStatusChanged;

// ============================================================================
// Utility: sendUi (used by ISRs, timers, WiFi task, etc.)
// ============================================================================
void sendUi(UiEventType t);

// ============================================================================
// LED helpers (inline, used by multiple modules)
// ============================================================================
static inline void okLedOn(bool on)  { digitalWrite(OK_LED_PIN, on ? HIGH : LOW); }
static inline void btnLedOn(bool on) { digitalWrite(BTN_LED_PIN, on ? HIGH : LOW); }
static inline void ledsAllOn()       { okLedOn(true);  btnLedOn(true); }
static inline void ledsAllOff()      { okLedOn(false); btnLedOn(false); }

// ============================================================================
// OLED power helpers (used by UI + WiFi + Chime tasks)
// ============================================================================
void oledSetPower(bool on);
void oledWakeFor(uint32_t ms);
void oledSetContrastSafe(uint8_t c);

// ============================================================================
// Settings persistence
// ============================================================================
void loadSettings();
void saveSettings();
void flushSettingsIfDirty();
inline void markSettingsDirty() { g_settingsDirty = true; }

// ============================================================================
// Splash status
// ============================================================================
void updateSplashStatus(const char* status);

// ============================================================================
// Time formatting utilities
// ============================================================================
void fmtHHMM(int minutes, char out[6]);
void fmtTimeNow(char out[12]);
void fmtMinutesToClock(int minutes, char* out, size_t outLen);
int16_t parseHHMMToMinutes(const char* s);
String dateDDMMYYYY(time_t t);
void applyTimezonePosix(const String& iana);
uint32_t dayKeyNowLocal();

// WiFi credential helpers
bool loadCreds(String& ssid, String& pass);
void saveCreds(const String& ssid, const String& pass);
void eraseCreds();

// Factory reset
void performFactoryReset();
