/*
 * UiTask.cpp — UI state machine, menu engine, display drawing, input, chime.
 */
#include "UiTask.h"
#include "DFPlayerManager.h"
#include "PrayerEngine.h"
#include "FotaManager.h"
#include "WifiManager.h"

#include "esp_pm.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

// ============================================================================
// Module-local state
// ============================================================================
static ArcIdleRenderer g_arcIdle;
static bool g_ledPulseActive = false;
static bool g_arcInit = false;
static ArcUserPrefs g_arcPrefs;
static InfoMode g_infoMode = INFO_NONE;

// Menu/list layout constants
static const int VISIBLE_ROWS = 4;
static const int ROW_H = 16;

// Button read helper
static inline bool pressedNow(int pin) { return digitalRead(pin) == HIGH; }

// Forward declarations (internal)
bool isStartupComplete();
static void ui_drawArcIdleScreen();
static void ui_drawPrayerScreen();
static void ui_drawPlaybackScreen();
static void ui_drawSplash();
static void drawMenuPage();
static void drawInfoPage(const char* title);
static void drawConfirmPage(const char* title);
static void drawFotaStatusPage();
static void drawCurrentSettingsPage();
static void drawAboutPage();
static void drawQiblaFinderPage();
static void drawListPage();
static void drawInfoPagePlain(const char* title, const String& line1, const String& line2 = String());

// ============================================================================
// Arc Idle Mode (replaces OLED OFF)
// ============================================================================
// --- UI (new architecture) ---
static uint32_t g_prayerScreenUntilMs = 0;
static bool     g_bannerBlinkPhase = false;


// Contrast levels (0..255)
static const uint8_t OLED_CONTRAST_ACTIVE = 255;
static const uint8_t OLED_CONTRAST_ARC    = 153; // ~60%

// Arc tuning
static const uint8_t ARC_THICKNESS_PX = 2;   // requested 2px; make this variable if needed
static const int     ARC_RADIUS_PX    = 54;  // tweak to taste for SH1106 128x64
static const int     ARC_CENTER_X     = 64;
static const int     ARC_CENTER_Y     = 62;  // near bottom so arc sits in upper area
static const uint8_t ARC_MARKER_RADIUS_PX = 2; // radius=2 → 5px diameter

// ============================================================================
// Menu timeout constant (used by OLED_ON_MS_MENU)
// ============================================================================
static const uint32_t MENU_IDLE_TIMEOUT_MS = 15000;
static uint32_t g_lastUiActionMs = 0;

// ============================================================================
// OLED Power Manager
// ============================================================================
static const uint32_t OLED_ON_MS_VOL   = 5000;
static const uint32_t OLED_ON_MS_MENU  = MENU_IDLE_TIMEOUT_MS + 5000; // 15s + 5s

// Wake-to-idle window (first OK when OLED is OFF)
static const uint32_t OLED_ON_MS_WAKE_IDLE = 15000;

// If non-zero: second OK within this window enters the menu
static volatile uint32_t g_okSecondPressUntilMs = 0;


////volatile bool     g_oledIsOn = true;
////volatile uint32_t g_oledOnUntilMs = 0;

// NOTE: oledHousekeeping depends on ScreenState + SCREEN_ARC_IDLE which are defined later,
// so we DECLARE it here and DEFINE it later (after ScreenState enum).

static volatile bool g_bootHoldOled = true;
static volatile bool g_bootHoldCleared = false;

// ============================================================================
// Power management
// ============================================================================
void enablePm() {
  esp_pm_config_esp32_t pm = {};
  pm.max_freq_mhz = 80;
  pm.min_freq_mhz = 80;
  pm.light_sleep_enable = false;
  esp_pm_configure(&pm);

  // Make sure BT is off if you don't use it
  btStop();

  // Optional: turn off unused RTC domains in light sleep (keep RTC mem if needed)
  // esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON); // if using RTC GPIO wake
}

// ============================================================================
// LED pulse
// ============================================================================
void pulseLeds(uint8_t mask, uint32_t ms) {
  btnLedOn(mask & 0x01);
  okLedOn(mask & 0x02);

  g_ledPulseActive = true;

  if (ledPulseTimer) {
    xTimerStop(ledPulseTimer, 0);
    xTimerChangePeriod(ledPulseTimer, pdMS_TO_TICKS(ms), 0);
    xTimerStart(ledPulseTimer, 0);
  }
}

// ============================================================================
// Display  <-- MUST be before oledSetPower() (uses display.oled_command)
// ============================================================================

// ============================================================================
// Display primitives
// ============================================================================
// ============================================================================
// Simple UI primitives
// ============================================================================

// WiFi RSSI -> 0..3 dots
uint8_t wifiStrengthDots() {
  if (WiFi.getMode() == WIFI_OFF) return 0;     // NEW
  if (WiFi.status() != WL_CONNECTED) return 0;
  int rssi = WiFi.RSSI();
  if (rssi >= -60) return 3;
  if (rssi >= -75) return 2;
  return 1;
}


// Volume index (0..3) -> 0..3 dots
uint8_t volumeDots(uint8_t volIdx) {
  if (volIdx == 0) return 0;
  if (volIdx >= 3) return 3;
  return volIdx; // 1->1, 2->2
}

void drawCentered(const char* txt, int y, uint8_t textSize) {
  display.setFont(nullptr);
  display.setTextSize(textSize);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(txt, 0, y, &x1, &y1, &w, &h);
  int x = (128 - (int)w) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(txt);
}

// ---- 3-dot indicator geometry (single source of truth) ----
inline int levelDotsR()   { return 2; }  // you said you changed radius to 2
inline int levelDotsGap() { return 4; }  // spacing between dot edges (keep as you like)

inline int levelDotsWidthPx3() {
  const int R = levelDotsR();
  const int GAP = levelDotsGap();
  const int N = 3;
  // total width from left edge of left dot to right edge of right dot
  return N*(2*R) + (N-1)*GAP;
}

// Draw 3 dots where xLeft is the LEFT EDGE of the dot group
void drawLevelDots3Left(int xLeft, int y, int level) {
  const int R = levelDotsR();
  const int GAP = levelDotsGap();
  const int STEP = 2*R + GAP;

  for (int i = 0; i < 3; i++) {
    int cx = xLeft + R + i * STEP;
    if (i < level) display.fillCircle(cx, y, R, SH110X_WHITE);
    else           display.drawCircle(cx, y, R, SH110X_WHITE);
  }
}

// Draw 3 dots where xRight is the RIGHT EDGE of the dot group
void drawLevelDots3Right(int xRight, int y, int level) {
  drawLevelDots3Left(xRight - levelDotsWidthPx3()-1, y, level);
}

// ============================================================================
// About page
// ============================================================================
void drawAboutPage() {
  xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setFont(nullptr);
  display.setTextSize(1);

  // ---- Lines ----
  int y = 1;

  display.setCursor(0, y);
  display.print("FW: ");
  display.print(FW_VER);
  y += 10;

  display.setCursor(0, y);
  display.print("Built: ");
  display.print(__DATE__);
  y += 10;

  display.setCursor(0, y);
  display.print("HW: ");
  display.print(HW_REV);
  y += 10;

  // ---- Network info ----
  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(0, y);
    display.print("IP: ");
    display.print(WiFi.localIP());
    y += 10;
  } else {
    display.setCursor(0, y);
    display.print("IP: --");
    y += 10;
  }

  // ---- Uptime ----
  display.setCursor(0, 41);
  display.print("Up: ");
  display.print(formatUptime());

  display.display();
  xSemaphoreGive(g_displayMtx);
}


// ============================================================================
// Splash screen
// ============================================================================
void ui_drawSplash() {
  xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  
  // Main title
  drawCentered("A:DHAN", 16, 2);
  drawCentered("Smart Salat Companion", 35, 1);
  // Version
  //drawCentered(FW_VER, 40, 1);
  
  // Status message at bottom (if any)
  if (g_splashMtx) xSemaphoreTake(g_splashMtx, portMAX_DELAY);
  bool hasStatus = (g_splashStatus.length() > 0);
  String displayText = g_splashStatus; // Make a copy while holding mutex
  if (g_splashMtx) xSemaphoreGive(g_splashMtx);
  
  if (hasStatus) {
    display.setFont(nullptr);
    display.setTextSize(1);
    
    // Truncate if too long (max ~21 chars at 6px font)
    if (displayText.length() > 21) {
      displayText = displayText.substring(0, 21) + "...";
    }
    
    // Center the status text at bottom
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(displayText.c_str(), 0, 0, &x1, &y1, &w, &h);
    int x = (128 - (int)w) / 2;
    if (x < 0) x = 0;
    
    display.setCursor(x, 56);  // Bottom of 64px screen
    display.print(displayText);
  }
  
  display.display();
  xSemaphoreGive(g_displayMtx);
}

// ============================================================================
// Qibla finder page
// ============================================================================
void drawQiblaFinderPage() {
  const int Y_OFF = -4;   // <<< shift everything UP by 4 px

  float deg = 0.0f;
  bool ok = computeQiblaBearingDeg(deg);

  const char* dir = "--";
  int dInt = 0;
  if (ok) {
    dir = bearingToCompass16(deg);
    dInt = (int)(deg + 0.5f);
  }

  xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  // ---- Direction (top, centered) ----
  drawCentered(dir, 6 + Y_OFF, 2);

  // ---- Angle number ----
  display.setFont(nullptr);
  display.setTextSize(2);

  char num[8];
  if (ok) snprintf(num, sizeof(num), "%d", dInt);
  else    snprintf(num, sizeof(num), "--");

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(num, 0, 0, &x1, &y1, &w, &h);

  const int yNum = 30 + Y_OFF;
  int xNum = (128 - (int)w) / 2;
  if (xNum < 0) xNum = 0;

  display.setCursor(xNum, yNum);
  display.print(num);

  // ---- Superscript degree mark ----
  if (ok) {
    const int xDeg = xNum + (int)w + 2;
    const int yDeg = yNum - 6;   // raised for superscript effect

    display.setTextSize(1);
    display.setCursor(xDeg, yDeg);
    display.print("o");
  }

  // ---- Footer (2 lines) ----
  display.setTextSize(1);
  drawCentered("Press any key", 52 + Y_OFF, 1);
  drawCentered("to return",     60 + Y_OFF, 1);

  display.display();
  xSemaphoreGive(g_displayMtx);
}

// ============================================================================
// Menu value formatter
// ============================================================================
// ============================================================================
// Menu value helper (depends on FOTA and settings globals)
// ============================================================================
const char* menuValueForLabel(const char* label) {
  if (!label) return nullptr;

  if (strcmp(label, "12 / 24 Hour") == 0)   return (g_timeFormat == 0) ? "12Hr" : "24Hr";
  if (strcmp(label, "Time Zone") == 0)    return nullptr;
  if (strcmp(label, "Method") == 0)       return nullptr;
  if (strcmp(label, "School") == 0)       return nullptr;
  if (strcmp(label, "Angle Method") == 0) return nullptr;
  if (strcmp(label, "Volume") == 0)         return VOL_NAMES[g_currentVolIdx];
  if (strcmp(label, "Adhan Type") == 0)     return (g_adhanType == 0) ? "Full" : (g_adhanType == 1) ? "Short" : "Chime";
  if (strcmp(label, "Dua") == 0)            return g_duaEnabled ? "On" : "Off";
  if (strcmp(label, "Countdown") == 0) return g_countdownBannerEnabled ? "On" : "Off";
  if (strcmp(label, "Firmware Update") == 0) {
    if (g_fotaBusy) return "...";
    if (g_fotaUpdateAvailable) return "UPD";
    return nullptr;
  }

  return nullptr;
}

// ============================================================================

// ============================================================================
// WiFi AP instructions screen
// ============================================================================
// ============================================================================
// OLED Draws
// ============================================================================
void ui_drawWifiApInstructions() {
  xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setFont(nullptr);
  display.setTextSize(1);

  // --- Page content (left-aligned, font size 1 = 6x8 px) ---
  // 128px wide / 6px per char = ~21 chars per line
  // 64px tall / 8px per line  = 8 lines max
  const int page = g_apOnboardPage;

  switch (page) {
    case 0:
      display.setCursor(0, 8);
      display.print("Welcome to");
      display.setCursor(0, 20);
      display.print("A:DHAN Setup");
      display.setCursor(0, 44);
      display.print("Press DOWN to start");
      break;

    case 1:
      display.setCursor(0, 4);
      display.print("Step 1:");
      display.setCursor(0, 18);
      display.print("On your phone/laptop");
      display.setCursor(0, 30);
      display.print("open Wi-Fi settings");
      display.setCursor(0, 42);
      display.print("and connect to:");
      display.setCursor(0, 54);
      display.print(AP_SSID);
      break;

    case 2:
      display.setCursor(0, 4);
      display.print("Step 2:");
      display.setCursor(0, 18);
      display.print("A setup page will");
      display.setCursor(0, 30);
      display.print("open automatically.");
      display.setCursor(0, 44);
      display.print("Enter your Wi-Fi &");
      display.setCursor(0, 54);
      display.print("prayer settings.");
      break;

    case 3:
      display.setCursor(0, 4);
      display.print("No popup?");
      display.setCursor(0, 20);
      display.print("Open a browser and");
      display.setCursor(0, 32);
      display.print("go to:");
      display.setCursor(0, 46);
      display.print("192.168.4.1");
      break;

    case 4:
      display.setCursor(0, 8);
      display.print("Almost done!");
      display.setCursor(0, 24);
      display.print("After saving, your");
      display.setCursor(0, 36);
      display.print("A:DHAN will restart.");
      display.setCursor(0, 52);
      display.print("That's it!");
      break;
  }

  // --- Navigation triangles (filled, ~8px each side) ---
  // Top-right: upward triangle (visible on pages 1-4)
  if (page > 0) {
    // Triangle pointing UP at top-right corner
    // Tip at top, base at bottom: centered around x=122, y~4
    display.fillTriangle(
      122, 1,    // top tip
      118, 7,    // bottom-left
      126, 7,    // bottom-right
      SH110X_WHITE
    );
  }

  // Bottom-right: downward triangle (visible on pages 0-3)
  if (page < AP_ONBOARD_PAGES - 1) {
    // Triangle pointing DOWN at bottom-right corner
    display.fillTriangle(
      122, 62,   // bottom tip
      118, 56,   // top-left
      126, 56,   // top-right
      SH110X_WHITE
    );
  }

  display.display();
  xSemaphoreGive(g_displayMtx);
}

void ui_drawFactoryResetScreen() {
  if (!g_displayMtx) return;
  xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  
  // Calculate remaining time
  uint32_t elapsed = millis() - g_factoryResetStartMs;
  uint32_t remaining = FACTORY_RESET_COUNTDOWN_MS;
  if (elapsed < FACTORY_RESET_COUNTDOWN_MS) {
    remaining = FACTORY_RESET_COUNTDOWN_MS - elapsed;
  } else {
    remaining = 0;
  }
  
  int secondsLeft = (remaining / 1000) + 1;  // Round up
  
  // Draw warning border (double border for emphasis)
  display.drawRect(5, 5, 118, 54, SH110X_WHITE);
  display.drawRect(6, 6, 116, 52, SH110X_WHITE);
  
  // Title
  display.setTextSize(1);
  display.setCursor(15, 12);
  display.print("! FACTORY RESET !");
  
  // Main message
  display.setTextSize(1);
  display.setCursor(23, 28);
  display.print("Resetting in");
  
  // Big countdown number
  display.setTextSize(2);
  char countStr[4];
  snprintf(countStr, sizeof(countStr), "%d", secondsLeft);
  
  // Center the countdown number
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(countStr, 0, 0, &x1, &y1, &w, &h);
  int x = (128 - w) / 2;
  display.setCursor(x, 38);
  display.print(countStr);
  
  // Release instruction (smaller text)
  display.setTextSize(1);
  display.setCursor(12, 52);
  display.print("Release to cancel");
  
  display.display();
  xSemaphoreGive(g_displayMtx);
}

void ui_drawArcIdleScreen() {
  // Only draw when we have valid time + prayer data
  if (!g_prayerReady || !g_today.valid()) return;
  if (!g_oledIsOn) return;
  if (!g_displayMtx) return;

  if (!g_arcInit) {
    g_arcIdle.begin(&display);
    g_arcPrefs.curvePadMin = 30;
    arcIdleApplyPrefs();
    arcIdleUpdateFromToday();
    g_arcInit = true;
  }

  // Snapshot current time (minute resolution)
  struct tm t;
  if (!getLocalTime(&t, 0)) return;
  const int nowMin = (t.tm_hour * 60) + t.tm_min;

  // Banner (NO FLASHING LOGIC — always steady when active)
  const bool bannerActive = (g_fotaUpdateAvailable && g_idleUpdateBanner);
  const char* bannerText  = "UPDATE AVAILABLE";

  // Volume dots (top-right)
  const uint8_t vd = volumeDots((uint8_t)g_currentVolIdx);
  const int yDots  = levelDotsR();

  // Determine which banner to show (priority: FOTA > WiFi > Countdown)
  bool showFotaBanner = (g_fotaUpdateAvailable && g_idleUpdateBanner);
  bool showWifiBanner = g_wifiConnectFailed;
  bool showCountdownBanner = false;
  char countdownBuf[32] = {0};
  
  // MODIFIED: Check if countdown banner is enabled by user
  if (!showFotaBanner && !showWifiBanner && g_countdownBannerEnabled) {
    // Only show countdown if:
    // 1. No other banners are showing
    // 2. User has enabled countdown banner in preferences
    showCountdownBanner = getNextPrayerCountdown(countdownBuf, sizeof(countdownBuf));
  }
  
  // Bottom banner geometry
  static constexpr int BANNER_H = 10;
  static constexpr int BANNER_Y = 64 - BANNER_H;
  
  xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  display.clearDisplay();
  
  // Arc scene
  g_arcIdle.render(nowMin);
  
  // Top-right volume dots
  display.setFont(nullptr);
  display.setTextSize(1);
  drawLevelDots3Right(128, yDots, vd);
  
  // Bottom banner (full width)
  if (showFotaBanner) {
    // FOTA banner (inverted: white BG, black text)
    display.fillRect(0, BANNER_Y, 128, BANNER_H, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    
    // Center "UPDATE AVAILABLE" text
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds("UPDATE AVAILABLE", 0, 0, &x1, &y1, &w, &h);
    int x = (128 - (int)w) / 2;
    int y = BANNER_Y + (BANNER_H - (int)h) / 2 - y1;
    
    display.setCursor(x, y);
    display.print("UPDATE AVAILABLE");
    
    display.setTextColor(SH110X_WHITE);
  } 
  else if (showWifiBanner) {
    // WiFi banner (inverted: white BG, black text)
    display.fillRect(0, BANNER_Y, 128, BANNER_H, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    
    // Center "NO WIFI CONNECTION" text
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds("NO WIFI CONNECTION", 0, 0, &x1, &y1, &w, &h);
    int x = (128 - (int)w) / 2;
    int y = BANNER_Y + (BANNER_H - (int)h) / 2 - y1;
    
    display.setCursor(x, y);
    display.print("NO WIFI CONNECTION");
    
    display.setTextColor(SH110X_WHITE);
  }
  else if (showCountdownBanner) {
    // Countdown banner (NON-inverted: black BG, white text)
    // Note: We don't fillRect because background is already black
    display.setTextColor(SH110X_WHITE);
    
    // Center countdown text
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(countdownBuf, 0, 0, &x1, &y1, &w, &h);
    int x = (128 - (int)w) / 2;
    int y = BANNER_Y + (BANNER_H - (int)h) / 2 - y1;
    
    display.setCursor(x, y);
    display.print(countdownBuf);
  }

  display.display();
  xSemaphoreGive(g_displayMtx);

  forceIdleRedraw = false;
}


// ============================================================================
// Prayer / Playback screens
// ============================================================================
void ui_drawPrayerScreen() {
  if (!g_prayerReady || !g_today.valid()) return;
  if (!g_oledIsOn) return;

  PrayerId nextId;
  int nextMin;
  bool fromTomorrow;
  computeNextPrayer(nextId, nextMin, fromTomorrow);

  char nextTime[12];
  fmtMinutesToClock(nextMin, nextTime, sizeof(nextTime));

  const uint8_t vd = volumeDots((uint8_t)g_currentVolIdx);

  xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setFont(nullptr);

  // Compact layout
  display.setTextSize(1);
  drawCentered("Next Prayer", 4, 1);  // Top of screen

  display.setTextSize(2);
  drawCentered(PRAYER_NAME[(uint8_t)nextId], 18, 2);  // Middle
  
  drawCentered(nextTime, 40, 2);  // Bottom
  
  display.display();
  xSemaphoreGive(g_displayMtx);
}

void ui_drawPlaybackScreen() {
  if (!g_oledIsOn) return;

  const uint8_t vd = volumeDots((uint8_t)g_currentVolIdx);
  const char* p = (g_playbackPrayerId != PR_NONE) ? PRAYER_NAME[(uint8_t)g_playbackPrayerId] : "ADHAN";

  xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setFont(nullptr);
  display.setTextSize(1);

  drawCentered(p, 18, 2);
  
  // Update the text based on mute state
  if (g_tempMuteActive) {
    drawCentered("MUTED", 52, 1);
  } else {
    drawCentered("Long press OK to MUTE", 52, 1);
  }

  // Volume dots (top-right)
  const int yDots = levelDotsR();
  drawLevelDots3Right(128, yDots, vd);

  display.display();
  xSemaphoreGive(g_displayMtx);
}

// ============================================================================
// Info / Settings / FOTA status pages
// ============================================================================
void drawCurrentSettingsPage() {
  int method, school, latadj;

  method = g_method;
  school = g_school;
  latadj = g_latAdj;

  const char* m = (method >= 0 && method < 24 && kMethodNames[method] && kMethodNames[method][0])
                    ? kMethodNames[method] : "Unknown";
  const char* s = (school >= 0 && school < 2) ? kSchoolNames[school] : "Unknown";
  const char* l = (latadj >= 0 && latadj < 4) ? kLatAdjNames[latadj] : "Unknown";

  xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Content moved up (no title)
  display.setCursor(0, 6);
  display.print("Method: ");
  display.print(m);

  display.setCursor(0, 20);
  display.print("School: ");
  display.print(s);

  display.setCursor(0, 34);
  display.print("Lat Adj: ");
  display.print(l);

  // Footer isolated at bottom
  drawCentered("Press any button", 56, 1);

  display.display();
  xSemaphoreGive(g_displayMtx);
}

void drawInfoPagePlain(const char* title, const String& line1, const String& line2) {
  xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  drawCentered(title ? title : "", 12, 1);

  if (line1.length()) drawCentered(line1.c_str(), 30, 1);
  if (line2.length()) drawCentered(line2.c_str(), 40, 1);

  drawCentered("Press any button", 56, 1);

  display.display();
  xSemaphoreGive(g_displayMtx);
}

void drawFotaStatusPage() {
  String s = fotaGetStatus();
  if (s.length() == 0) {
    if (g_fotaUpdateAvailable && g_fotaLatestCached.length()) {
      s = "Update: " + g_fotaLatestCached;
    } else {
      s = "Up to date";
    }
  }
  drawInfoPagePlain("Firmware Update", s);
}


// ============================================================================
// Timer callbacks
// ============================================================================
void splashTimerCb(TimerHandle_t) {
  // If startup is complete, hide splash
  if (isStartupComplete()) {
    sendUi(UI_EVT_SPLASH_DONE);
  } else {
    // Extend splash a bit more
    xTimerChangePeriod(splashTimer, pdMS_TO_TICKS(1000), 0);
    xTimerStart(splashTimer, 0);
    
    // // Update status to show what's still loading
    // if (!g_timeSynced) {
    //   updateSplashStatus("Syncing time...");
    // } else if (!g_locationReady) {
    //   updateSplashStatus("Fetching Location...");
    // } else if (!g_prayerReady) {
    //   updateSplashStatus("Fetching Prayers...");
    // } else {
    //   updateSplashStatus("Finishing startup...");
    // }
  }
}

void tickTimerCb(TimerHandle_t)      { sendUi(UI_EVT_TICK); }

void ledPulseTimerCb(TimerHandle_t)  { ledsAllOff(); g_ledPulseActive = false; }


// ============================================================================
// ISRs
// ============================================================================
void IRAM_ATTR isrBtnUp() {
  if (!rawBtnQueue) return;
  RawBtnEvent ev{ RAW_BTN_UP, (uint32_t)millis() };
  BaseType_t hp = pdFALSE;
  xQueueSendFromISR(rawBtnQueue, &ev, &hp);
  if (hp) portYIELD_FROM_ISR();
}

void IRAM_ATTR isrBtnDn() {
  if (!rawBtnQueue) return;
  RawBtnEvent ev{ RAW_BTN_DN, (uint32_t)millis() };
  BaseType_t hp = pdFALSE;
  xQueueSendFromISR(rawBtnQueue, &ev, &hp);
  if (hp) portYIELD_FROM_ISR();
}

void IRAM_ATTR isrBtnOk() {
  if (!rawBtnQueue) return;
  RawBtnEvent ev{ RAW_BTN_OK, (uint32_t)millis() };
  BaseType_t hp = pdFALSE;
  xQueueSendFromISR(rawBtnQueue, &ev, &hp);
  if (hp) portYIELD_FROM_ISR();
}

// In the ISR function for DF_BUSY pin
void IRAM_ATTR isrDfBusy() {
  if (!uiQueue) return;
  
  // If DFPlayer is not powered, ignore BUSY pin changes
  if (!g_dfPowered) {
    g_dfIsPlaying = false;
    g_dfBusyLevel = 1; // Assume HIGH when off
    return;
  }
  
  // Sample the pin quickly
  uint8_t lvl = (uint8_t)digitalRead(DF_BUSY_PIN);
  uint8_t prevLevel = g_dfBusyLevel;
  g_dfBusyLevel = lvl;

  // Only update playing state if DFPlayer is powered
  bool wasPlaying = g_dfIsPlaying;
  g_dfIsPlaying = (lvl == LOW);
  
  // Check if playback just ended (HIGH after LOW)
  if (wasPlaying && !g_dfIsPlaying) {
    // Playback finished - restore temporary mute if active
    if (g_tempMuteActive) {
      // We'll handle this in the UI task to avoid ISR complexity
      // Just send an event
    }
  }

  UiEvent ev{ UI_EVT_DF_BUSY_CHANGED };
  BaseType_t hp = pdFALSE;
  xQueueSendFromISR(uiQueue, &ev, &hp);
  if (hp) portYIELD_FROM_ISR();
}

// ============================================================================
// Input task
// ============================================================================
void inputTask(void*) {
  LOGI(LOG_TAG_BTN, "Input task started");
  const uint32_t debounceMs = 80;
  uint32_t lastUp = 0, lastDn = 0, lastOk = 0;

  for (;;) {
    RawBtnEvent raw;
    if (xQueueReceive(rawBtnQueue, &raw, portMAX_DELAY) == pdTRUE) {
      uint32_t now = raw.isrMs;

      if (raw.id == RAW_BTN_UP) {
        if ((now - lastUp) < debounceMs) continue;
        vTaskDelay(pdMS_TO_TICKS(15));
        if (pressedNow(BTN_UP)) { lastUp = now; sendUi(UI_EVT_BTN_UP); }
      }
      else if (raw.id == RAW_BTN_DN) {
        if ((now - lastDn) < debounceMs) continue;
        vTaskDelay(pdMS_TO_TICKS(15));
        if (pressedNow(BTN_DN)) { lastDn = now; sendUi(UI_EVT_BTN_DN); }
      }
      else { // RAW_BTN_OK
        if ((now - lastOk) < debounceMs) continue;
        vTaskDelay(pdMS_TO_TICKS(15));
        if (!pressedNow(BTN_OK)) continue;

        // We have a valid press
        lastOk = now;

        const uint32_t LONG_MS = 900;              // Regular long press
        const uint32_t FACTORY_RESET_HOLD_MS = 10000;  // 10 seconds for factory reset
        const uint32_t POLL_MS = 20;

        uint32_t t0 = millis();
        bool factoryResetTriggered = false;
        
        // Wait and check hold duration
        while (pressedNow(BTN_OK)) {
          uint32_t elapsed = millis() - t0;
          
          // Check for factory reset trigger (10 seconds)
          if (!factoryResetTriggered && elapsed >= FACTORY_RESET_HOLD_MS) {
            LOGI(LOG_TAG_BTN, "Factory reset triggered - 10s hold detected");
            sendUi(UI_EVT_FACTORY_RESET_TRIGGER);
            factoryResetTriggered = true;
          }
          
          vTaskDelay(pdMS_TO_TICKS(POLL_MS));
        }

        // Button released - determine what type of press it was
        uint32_t holdTime = millis() - t0;
        
        if (factoryResetTriggered) {
          // Factory reset was triggered
          if (holdTime >= (FACTORY_RESET_HOLD_MS + FACTORY_RESET_COUNTDOWN_MS)) {
            // Held through entire countdown - reset will happen automatically
            LOGI(LOG_TAG_BTN, "Factory reset confirmed - held for %lu ms", holdTime);
          } else {
            // Released during countdown - send cancel event
            LOGI(LOG_TAG_BTN, "Factory reset cancelled - released at %lu ms", holdTime);
            sendUi(UI_EVT_FACTORY_RESET_CANCEL);
          }
        } else if (holdTime >= LONG_MS) {
          // Long press (but not factory reset)
          sendUi(UI_EVT_BTN_OK_LONG);
        } else {
          // Short press
          sendUi(UI_EVT_BTN_OK);
        }
      }
    }
  }
}

// ============================================================================
// List picker
// ============================================================================
// ============================================================================
// LIST PICKER (4 rows, inverse highlight)
// ============================================================================
enum ListKind : uint8_t {
  LIST_TZ,
  LIST_METHOD,
  LIST_SCHOOL,
  LIST_LATADJ
};

volatile ListKind g_listKind = LIST_METHOD;
const char* g_listTitle = nullptr;

int g_listSel = 0;
int g_listTop = 0;

// ---- Timezone list must match portal supported zones ----
// Index 0 = Auto ("")
const char* kTzList[] = {
  "Auto (from Wi-Fi/IP)",
  "Europe/London",
  "Europe/Copenhagen",
  "Europe/Oslo",
  "Europe/Stockholm",
  "Europe/Paris",
  "Europe/Madrid",
  "Europe/Rome",
  "Europe/Berlin",
  "Europe/Helsinki",
  "America/New_York",
  "America/Chicago",
  "America/Denver",
  "America/Los_Angeles",
  "Australia/Sydney",
  "Pacific/Auckland"
};

inline int listCount(ListKind k) {
  int n = 0;
  switch (k) {
    case LIST_TZ:     n = (int)(sizeof(kTzList)/sizeof(kTzList[0])); break;
    case LIST_METHOD: n = 0; for (int i=0;i<24;i++) if (kMethodNames[i] && kMethodNames[i][0]) n++; break;
    case LIST_SCHOOL: n = 2; break;
    case LIST_LATADJ: n = 4; break;
  }
  return n + 1; // + "Back"
}

inline bool listIsBack(ListKind k, int idx) {
  return idx == (listCount(k) - 1);
}

inline const char* listLabel(ListKind k, int idx) {
  if (listIsBack(k, idx)) return "Back";

  switch (k) {
    case LIST_TZ:
      return kTzList[idx];

    case LIST_SCHOOL:
      return kSchoolNames[idx];

    case LIST_LATADJ:
      return kLatAdjNames[idx];

    case LIST_METHOD: {
      // method list is sparse because you skip blanks; idx is "visible index"
      int vis = -1;
      for (int i=0;i<24;i++) {
        if (!kMethodNames[i] || !kMethodNames[i][0]) continue;
        vis++;
        if (vis == idx) return kMethodNames[i];
      }
      return "?";
    }
  }
  return "?";
}

// Map list index -> setting value (method/school/latadj) or tz string.
// For "Back" caller must check listIsBack() first.
inline int listValueInt(ListKind k, int idx) {
  switch (k) {
    case LIST_SCHOOL: return idx;          // 0..1
    case LIST_LATADJ: return idx;          // 0..3
    case LIST_METHOD: {
      int vis = -1;
      for (int i=0;i<24;i++) {
        if (!kMethodNames[i] || !kMethodNames[i][0]) continue;
        vis++;
        if (vis == idx) return i;          // returns true method id
      }
      return 2;
    }
    default:
      return 0;
  }
}

inline String listValueTz(int idx) {
  if (idx <= 0) return "";                 // Auto
  return String(kTzList[idx]);
}

// Current selection -> list index (so current option is focused)
inline int listIndexFromCurrent(ListKind k) {
  switch (k) {
    case LIST_TZ: {
      if (g_tzOverride.length() == 0) return 0;
      for (int i=1; i<(int)(sizeof(kTzList)/sizeof(kTzList[0])); i++) {
        if (g_tzOverride.equals(kTzList[i])) return i;
      }
      return 0;
    }
    case LIST_SCHOOL:
      return (g_school < 0 || g_school > 1) ? 0 : g_school;

    case LIST_LATADJ:
      return (g_latAdj < 0 || g_latAdj > 3) ? 0 : g_latAdj;

    case LIST_METHOD: {
      // Convert actual method id -> "visible index"
      int vis = -1;
      for (int i=0;i<24;i++) {
        if (!kMethodNames[i] || !kMethodNames[i][0]) continue;
        vis++;
        if (i == g_method) return vis;
      }
      return 0;
    }
  }
  return 0;
}

inline void listEnsureVisible() {
  const int n = listCount(g_listKind);
  const int centerRow = 1; // keep selection on 2nd visible row

  // Desired top so that sel appears on centerRow
  int desiredTop = g_listSel - centerRow;

  // Clamp to valid window range
  int maxTop = n - (int)VISIBLE_ROWS;
  if (maxTop < 0) maxTop = 0;

  if (desiredTop < 0) desiredTop = 0;
  if (desiredTop > maxTop) desiredTop = maxTop;

  g_listTop = desiredTop;
}

void drawListPage() {
  listEnsureVisible();

  xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextSize(1);

  int16_t x1, y1; uint16_t tw, th;
  display.getTextBounds("Ag", 0, 0, &x1, &y1, &tw, &th);

  for (int row=0; row<VISIBLE_ROWS; row++) {
    int idx = g_listTop + row;
    if (idx >= listCount(g_listKind)) break;

    const bool isSel = (idx == g_listSel);
    const int yTop = row * ROW_H;   // FULL HEIGHT rows now (0,16,32,48)

    if (isSel) {
      display.fillRect(0, yTop, 128, ROW_H, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }

    const int yTextTop = yTop + (ROW_H - (int)th) / 2;
    const int cursorY  = yTextTop - y1;

    display.setCursor(6, cursorY);
    display.print(listLabel(g_listKind, idx));
  }

  display.display();
  xSemaphoreGive(g_displayMtx);
}

inline void startList(ListKind kind) {
  g_listKind = kind;
  g_listSel = listIndexFromCurrent(kind);
  g_listTop = 0;
  listEnsureVisible();
}


// ============================================================================
// Screen state helpers
// ============================================================================

inline bool uiIsMenuLike(ScreenState s) {
  return (s == SCREEN_MENU_ENGINE ||
          s == SCREEN_LIST ||
          s == SCREEN_INFO ||
          s == SCREEN_CONFIRM ||
          s == SCREEN_WIFI_STATUS ||
          s == SCREEN_WIFI_AP);
}

// In menus: both LEDs ON (OK behaves like BTN). Outside menus: don't force here.
inline void menuLedsUpdate(ScreenState s) {
  if (uiIsMenuLike(s)) {
    okLedOn(true);
    btnLedOn(true);
  }
}

inline void oledHousekeeping(ScreenState /*state*/) {
  // New UI architecture: ARC IDLE is the primary idle screen (OLED stays on).
  // Any display power-saving/dimming policy can be reintroduced later as a separate layer.
}

// ============================================================================
// Menu engine
// ============================================================================
// ============================================================================
// MENU ENGINE (stack-based, 4 visible rows)
// ============================================================================
enum MenuItemType : uint8_t { MI_SUBMENU, MI_ACTION, MI_BACK, MI_INFO, MI_CONFIRM };

struct MenuPage;
typedef void (*MenuActionFn)();

struct MenuItem {
  const char* label;
  MenuItemType type;
  const MenuPage* submenu;     // MI_SUBMENU
  MenuActionFn action;         // MI_ACTION / MI_CONFIRM
  const char* infoTitle;       // MI_INFO
};

struct MenuPage {
  const char* title;
  const MenuItem* items;
  uint8_t count;
};

struct MenuCtx {
  const MenuPage* page;
  int sel;
  int top;
};

MenuCtx g_menuStack[6];
int g_menuDepth = -1;

bool isStartupComplete() {
  // Check if we have the essentials
  bool hasTime = g_timeSynced;
  bool hasLocation = g_locationReady;
  bool hasPrayers = g_prayerReady;
  
  // Also check if UI is ready (menu system initialized)
  bool uiReady = (g_menuDepth == -1 && g_infoMode == INFO_NONE);
  
  return hasTime && hasLocation && hasPrayers && uiReady;
}

// confirm dialog state
const char* g_confirmTitle = nullptr;
MenuActionFn g_confirmProceed = nullptr;
int g_confirmSel = 0; // 0=Proceed, 1=Return

void menuPush(const MenuPage* p) {
  if (!p) return;
  if (g_menuDepth < (int)(sizeof(g_menuStack)/sizeof(g_menuStack[0])) - 1) {
    g_menuDepth++;
    g_menuStack[g_menuDepth] = { p, 0, 0 };
  } else {
    g_menuStack[g_menuDepth] = { p, 0, 0 };
  }
}

void menuPop() {
  if (g_menuDepth > 0) g_menuDepth--;
}

MenuCtx& menuCur() { return g_menuStack[g_menuDepth]; }

void menuEnsureVisible(MenuCtx& c) {
  if (c.sel < c.top) c.top = c.sel;
  if (c.sel >= c.top + VISIBLE_ROWS) c.top = c.sel - (VISIBLE_ROWS - 1);
  if (c.top < 0) c.top = 0;
  int maxTop = (int)c.page->count - (int)VISIBLE_ROWS;
  if (maxTop < 0) maxTop = 0;
  if (c.top > maxTop) c.top = maxTop;
}

// ---- Actions you already have via queues ----
void actionStartApPortal() {
  WifiCmd cmd{ WIFI_CMD_START_AP_PORTAL };
  xQueueSend(wifiCmdQueue, &cmd, 0);
}

void actionForgetNetwork() {
  WifiCmd cmd{ WIFI_CMD_FORGET_AND_START_AP };
  xQueueSend(wifiCmdQueue, &cmd, 0);
}

void actionResetDevice() {
  WifiCmd cmd{ WIFI_CMD_FORGET_AND_START_AP };
  xQueueSend(wifiCmdQueue, &cmd, 0);
}

// Device
void actToggleTimeFormat() {
  g_timeFormat = (g_timeFormat == 0) ? 1 : 0;
  markSettingsDirty();
  arcIdleApplyPrefs();
  forceIdleRedraw = true;
}

// Adhan
void actCycleAdhanType() {
  g_adhanType = (uint8_t)((g_adhanType + 1) % 3);
  markSettingsDirty();
}

void actToggleDua() {
  g_duaEnabled = !g_duaEnabled;
  markSettingsDirty();
}

void actCycleVolume() {
  g_currentVolIdx = (uint8_t)((g_currentVolIdx + 1) % 4);
  if (g_dfOk) dfSetVolumeSafe((uint8_t)VOL_LEVELS[g_currentVolIdx]);
  markSettingsDirty();
}

void actRefreshNow() {
  WifiCmd cmd{ WIFI_CMD_RUN_DAILY_REFRESH };
  xQueueSend(wifiCmdQueue, &cmd, 0);
}

void actCheckUpdates() {
  g_infoMode = INFO_FOTA_STATUS;
  if (g_fotaBusy) return;

  g_fotaBusy = true;
  fotaSetStatus("Starting...");

  WifiCmd cmd{ WIFI_CMD_RUN_FOTA_CHECK };
  xQueueSend(wifiCmdQueue, &cmd, 0);

  // When session completes, set g_fotaBusy=false (see note below)
}

void actToggleCountdownBanner() {
  g_countdownBannerEnabled = !g_countdownBannerEnabled;
  markSettingsDirty();
  forceIdleRedraw = true;
  
  LOGI(LOG_TAG_UI, "Countdown banner %s", 
       g_countdownBannerEnabled ? "ENABLED" : "DISABLED");
}

// Remove in production
#ifdef DEV_BUILD
void actTestAdhan() {
  if (g_currentVolIdx == 0) {
    LOGI(LOG_TAG_ADHAN, "Test adhan skipped (MUTE)");
    return;
  }

  uint8_t track = 4;
  uint32_t safetyOffMs = 20000;

  if (g_adhanType == 0) {        // FULL
    track = 2;
    safetyOffMs = 130000;        // a bit > expected full length
  } else if (g_adhanType == 1) { // SHORT
    track = 3;
    safetyOffMs = 60000;         // safety, but BUSY end will stop earlier
  } else {                       // CHIME
    track = 4;
    safetyOffMs = 30000;
  }

  LOGI(LOG_TAG_ADHAN, "Test adhan: type=%u track=%u volIdx=%u (vol=%u)",
       (unsigned)g_adhanType,
       (unsigned)track,
       (unsigned)g_currentVolIdx,
       (unsigned)VOL_LEVELS[g_currentVolIdx]);

  oledWakeFor(safetyOffMs + 5000);

  dfPowerOn();
  if (!g_dfOk) {
    LOGW(LOG_TAG_ADHAN, "DF not ready for test");
    dfScheduleOff(1500);
    return;
  }

  dfSetVolumeSafe((uint8_t)VOL_LEVELS[g_currentVolIdx]);

  const uint8_t busyBefore = (uint8_t)digitalRead(DF_BUSY_PIN);
  LOGI(LOG_TAG_ADHAN, "DF_BUSY before play: level=%u (playing=%d)",
       (unsigned)busyBefore, (busyBefore == LOW) ? 1 : 0);

  // Mark "starting" for UI
  g_playbackPrayerId = PR_NONE;
  g_dfStarting = true;
  g_dfPlayCmdAtMs = millis();
  forceIdleRedraw = true;

  // Start playback
  dfPlayFolderSafe(1, track);

  // Wait for BUSY assert (observed ~750ms on your module)
  const uint32_t BUSY_ASSERT_TIMEOUT_MS = 2000;
  bool asserted = false;

  while ((millis() - g_dfPlayCmdAtMs) < BUSY_ASSERT_TIMEOUT_MS) {
    if (digitalRead(DF_BUSY_PIN) == LOW) { asserted = true; break; }
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  const uint32_t dt = millis() - g_dfPlayCmdAtMs;
  const uint8_t busyAfter = (uint8_t)digitalRead(DF_BUSY_PIN);

  LOGI(LOG_TAG_ADHAN, "DF_BUSY after play:  level=%u (playing=%d) assert=%d dt=%lu ms",
       (unsigned)busyAfter, (busyAfter == LOW) ? 1 : 0,
       asserted ? 1 : 0,
       (unsigned long)dt);

  // If BUSY never asserted, don’t stay in "starting" forever
  if (!asserted) {
    g_dfStarting = false;
    LOGW(LOG_TAG_ADHAN, "DF_BUSY did not assert within %lu ms", (unsigned long)BUSY_ASSERT_TIMEOUT_MS);
    forceIdleRedraw = true;
  }

  // Safety power-off (BUSY end handler will schedule an earlier off)
  dfScheduleOff(safetyOffMs);

  LOGI(LOG_TAG_ADHAN, "Test adhan started: folder=1 track=%u, safety off in %lu ms",
       (unsigned)track, (unsigned long)safetyOffMs);
}

void actForceUpdateBannerToggle() {
  g_fotaUpdateAvailable = !g_fotaUpdateAvailable;
  g_idleUpdateBanner    = g_fotaUpdateAvailable; // show banner when “available”
  g_fotaLatestCached    = g_fotaUpdateAvailable ? "9.9.9" : "";
  saveSettings();        // optional
  forceIdleRedraw = true;
}

void actTestWifiBanner() {
  // Toggle WiFi failure banner for testing
  g_wifiConnectFailed = !g_wifiConnectFailed;
  forceIdleRedraw = true;
  
  LOGI(LOG_TAG_UI, "WiFi banner test: %s", g_wifiConnectFailed ? "SHOW" : "HIDE");
}

void actTestArcPositions() {
  // Cycle through different times to test arc progress
  static int testHour = 0;
  testHour = (testHour + 6) % 24;
  
  // Simulate time for testing (affects arc display only)
  // You'd need to modify g_arcIdle.render() to accept test time
  LOGI(LOG_TAG_UI, "Test arc position: %02d:00", testHour);
}

void actTestBannerPriority() {
  // Test both banners ON to see priority
  g_fotaUpdateAvailable = true;
  g_idleUpdateBanner = true;
  g_wifiConnectFailed = true;
  forceIdleRedraw = true;
  LOGI(LOG_TAG_UI, "Both banners ON (FOTA should have priority)");
}

void actClearAllBanners() {
  g_fotaUpdateAvailable = false;
  g_idleUpdateBanner = false;
  g_wifiConnectFailed = false;
  forceIdleRedraw = true;
  LOGI(LOG_TAG_UI, "All banners cleared");
}

#endif

// ---- Draw helpers ----
void drawMenuPage() {
  MenuCtx c = menuCur();
  menuEnsureVisible(c);

  xSemaphoreTake(g_displayMtx, portMAX_DELAY);

  display.clearDisplay();
  display.setFont(nullptr);
  display.setTextSize(1);

  // Measure default font height (6x8) reliably
  int16_t x1, y1;
  uint16_t tw, th;
  display.getTextBounds("Ag", 0, 0, &x1, &y1, &tw, &th);

  const int leftPad = 6;

  for (int row = 0; row < VISIBLE_ROWS; row++) {
    const int idx = c.top + row;
    if (idx >= c.page->count) break;

    const bool isSel = (idx == c.sel);
    const int yTop = row * ROW_H;

    if (isSel) {
      display.fillRect(0, yTop, 128, ROW_H, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }

    const int yTextTop = yTop + (ROW_H - (int)th) / 2;
    const int cursorY  = yTextTop - y1;

    display.setCursor(leftPad, cursorY);
    display.print(c.page->items[idx].label);

    const char* v = menuValueForLabel(c.page->items[idx].label);
    if (v) {
      int16_t vx1, vy1; uint16_t vw, vh;
      display.getTextBounds(v, 0, 0, &vx1, &vy1, &vw, &vh);
      display.setCursor(128 - 6 - (int)vw, cursorY);
      display.print(v);
    }
  }

  display.display();
  xSemaphoreGive(g_displayMtx);

  g_menuStack[g_menuDepth] = c;
}

void drawInfoPage(const char* title) {
  xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  const int y = levelDotsR(); // or just 2
  drawLevelDots3Left(0, y, wifiStrengthDots());
  drawLevelDots3Right(128, y, volumeDots((uint8_t)g_currentVolIdx));

  drawCentered(title ? title : "", 28, 1);
  drawCentered("Press any button", 50, 1);

  display.display();
  xSemaphoreGive(g_displayMtx);
}

void drawConfirmPage(const char* title) {
  xSemaphoreTake(g_displayMtx, portMAX_DELAY);
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  drawCentered(title ? title : "Confirm", 18, 1);

  const char* a = "Proceed";
  const char* b = "Return";

  int y0 = 34;
  for (int i = 0; i < 2; i++) {
    bool isSel = (i == g_confirmSel);
    int yTop = y0 + i * ROW_H;

    if (isSel) {
      display.fillRect(0, yTop, 128, ROW_H, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }
    drawCentered(i==0 ? a : b, yTop + 4, 1);
  }

  display.display();
  xSemaphoreGive(g_displayMtx);
}

void confirmStart(const char* title, MenuActionFn proceedFn) {
  g_confirmTitle = title;
  g_confirmProceed = proceedFn;
  g_confirmSel = 0;
  drawConfirmPage(g_confirmTitle);
}

// ================= Define your menu tree =================
// ---------- Preferences ----------
const MenuItem PREF_ITEMS[] = {
  { "Volume",      MI_ACTION, nullptr, actCycleVolume,   nullptr },
  { "Adhan Type",  MI_ACTION, nullptr, actCycleAdhanType,nullptr },
  { "Dua",         MI_ACTION, nullptr, actToggleDua,     nullptr },
  { "Countdown",   MI_ACTION, nullptr, actToggleCountdownBanner, nullptr },
  { "Back",        MI_BACK,   nullptr, nullptr,          nullptr },
};

const MenuPage PAGE_PREFERENCES = { "Preferences", PREF_ITEMS, (uint8_t)(sizeof(PREF_ITEMS)/sizeof(PREF_ITEMS[0])) };


// ---------- Time ----------
const MenuItem TIME_ITEMS[] = {
  { "12 / 24 Hour", MI_ACTION, nullptr, actToggleTimeFormat, nullptr },
  { "Time Zone",    MI_ACTION, nullptr, nullptr,            nullptr }, // special-cased
  { "Back",         MI_BACK,   nullptr, nullptr,            nullptr },
};

const MenuPage PAGE_TIME = { "Time", TIME_ITEMS, (uint8_t)(sizeof(TIME_ITEMS)/sizeof(TIME_ITEMS[0])) };

const MenuItem PRAYER_ITEMS[] = {
  { "Method",       MI_ACTION, nullptr, nullptr, nullptr }, // special-cased
  { "School",       MI_ACTION, nullptr, nullptr, nullptr }, // special-cased
  { "Angle Method", MI_ACTION, nullptr, nullptr, nullptr }, // special-cased (your latAdj)
  { "Back",         MI_BACK,   nullptr, nullptr, nullptr },
};

const MenuPage PAGE_PRAYER = { "Prayer", PRAYER_ITEMS, (uint8_t)(sizeof(PRAYER_ITEMS)/sizeof(PRAYER_ITEMS[0])) };


// ---------- Network ----------
const MenuItem NETWORK_ITEMS[] = {
  { "Wi-Fi Status",  MI_ACTION,  nullptr, nullptr,           nullptr }, // special-cased in OK handler
  { "Reset Device",  MI_CONFIRM, nullptr, actionResetDevice, nullptr }, // confirm -> forget creds + AP mode
  { "Back",          MI_BACK,    nullptr, nullptr,           nullptr },
};

const MenuPage PAGE_NETWORK = { "Network", NETWORK_ITEMS, (uint8_t)(sizeof(NETWORK_ITEMS)/sizeof(NETWORK_ITEMS[0])) };


// ---------- System ----------
const MenuItem SYSTEM_ITEMS[] = {
  { "Firmware Update", MI_ACTION, nullptr, actCheckUpdates, nullptr }, // show status/progress
  { "About",           MI_ACTION, nullptr, nullptr,         nullptr }, // special-cased in OK handler
  { "Back",            MI_BACK,   nullptr, nullptr,         nullptr },
};

const MenuPage PAGE_SYSTEM = { "System", SYSTEM_ITEMS, (uint8_t)(sizeof(SYSTEM_ITEMS)/sizeof(SYSTEM_ITEMS[0])) };

#ifdef DEV_BUILD
// Updated TEST_ITEMS with more options:
const MenuItem TEST_ITEMS[] = {
  { "Test Adhan",           MI_ACTION, nullptr, actTestAdhan,               nullptr },
  { "Toggle FOTA Banner",   MI_ACTION, nullptr, actForceUpdateBannerToggle, nullptr },
  { "Toggle WiFi Banner",   MI_ACTION, nullptr, actTestWifiBanner,          nullptr },
  { "Banner Priority",      MI_ACTION, nullptr, actTestBannerPriority,      nullptr },
  { "Clear Banners",        MI_ACTION, nullptr, actClearAllBanners,         nullptr },
  { "Test Arc Positions",   MI_ACTION, nullptr, actTestArcPositions,        nullptr },
  { "Back",                 MI_BACK,   nullptr, nullptr,                    nullptr },
};

const MenuPage PAGE_TEST = { "Test", TEST_ITEMS, (uint8_t)(sizeof(TEST_ITEMS)/sizeof(TEST_ITEMS[0])) };
#endif

// ---------- Root ----------
const MenuItem ROOT_ITEMS[] = {
  { "Preferences",     MI_SUBMENU, &PAGE_PREFERENCES, nullptr, nullptr },
  { "Time",            MI_SUBMENU, &PAGE_TIME,        nullptr, nullptr },
  { "Prayer",          MI_SUBMENU, &PAGE_PRAYER,      nullptr, nullptr },
  { "Qibla Direction", MI_ACTION,  nullptr,           nullptr, nullptr }, // special-cased -> INFO_QIBLA
  { "Network",         MI_SUBMENU, &PAGE_NETWORK,     nullptr, nullptr },
  { "System",          MI_SUBMENU, &PAGE_SYSTEM,      nullptr, nullptr },
  #ifdef DEV_BUILD
    { "Test",            MI_SUBMENU, &PAGE_TEST,        nullptr, nullptr },
  #endif
  { "Back",            MI_BACK,    nullptr,           nullptr, nullptr },
};

const MenuPage PAGE_ROOT = { "Menu", ROOT_ITEMS, (uint8_t)(sizeof(ROOT_ITEMS)/sizeof(ROOT_ITEMS[0])) };

inline void noteUiAction() { 
  g_lastUiActionMs = millis();
  LOGI(LOG_TAG_UI, "noteUiAction: reset timer to %lu", g_lastUiActionMs); // Keep this for debugging
}

inline void noteMenuAction() {
  g_lastUiActionMs = millis();
  oledWakeFor(OLED_ON_MS_MENU);
}

// Exit to idle the same way "Return" does (including deferred save if needed)
inline bool menuTimedOut() {
  bool timedOut = g_lastUiActionMs != 0 && (millis() - g_lastUiActionMs) > MENU_IDLE_TIMEOUT_MS;
  if (timedOut) {
    LOGI(LOG_TAG_UI, "Menu timed out! g_lastUiActionMs=%lu, millis()=%lu, diff=%lu, timeout=%lu",
         g_lastUiActionMs, millis(), millis() - g_lastUiActionMs, MENU_IDLE_TIMEOUT_MS);
  }
  return timedOut;
}

// ============================================================================
// UI Task
// ============================================================================
void exitToIdleFromMenu(ScreenState &state) {
    LOGI(LOG_TAG_UI, "exitToIdleFromMenu called, current state=%d", state);
    
    // Save any pending settings
    flushSettingsIfDirty();
    
    // Reset menu state
    g_menuDepth = -1;
    g_infoMode = INFO_NONE;
    g_lastUiActionMs = 0;  // Reset timeout timer
    LOGI(LOG_TAG_UI, "Reset g_lastUiActionMs to 0");
    
    // Go to idle
    state = SCREEN_ARC_IDLE;
    ledsAllOff(); 

    // Force idle screen redraw
    forceIdleRedraw = true;
    
    if (g_oledIsOn) {
      if (g_dfIsPlaying) ui_drawPlaybackScreen();
      else               ui_drawArcIdleScreen();
    }
    LOGI(LOG_TAG_UI, "Switched to SCREEN_ARC_IDLE, display should be updated");
}

inline void bootHoldMaybeClear() {
  if (g_bootHoldCleared) return;

  // If prayers are already ready (event may have happened while not in IDLE),
  // clear boot hold now so arc mode can work.
  if (g_prayerReady) {
    g_bootHoldOled = false;
    g_bootHoldCleared = true;

    // Start a normal idle window if none is running yet
    if (g_oledOnUntilMs == 0) {
      oledWakeFor(8000);
    }

    forceIdleRedraw = true;
    LOGI(LOG_TAG_OLED, "Boot hold cleared (prayers ready)");
  }
}

// ============================================================================
// UI Task
// ============================================================================
void uiTask(void*) {
  LOGI(LOG_TAG_UI, "UI task started");
  ScreenState state = SCREEN_SPLASH;
  noteUiAction();

  ui_drawSplash();
  xTimerStart(splashTimer, 0);
  xTimerStart(tickTimer, 0);

  // Housekeeping timing (so we don't depend on UI_EVT_TICK delivery)
  uint32_t lastIdleRedrawMs = 0;
  uint32_t lastWifiStatusRedrawMs = 0;
  uint32_t lastApRedrawMs = 0;
  uint32_t lastInfoRedrawMs = 0;

  // Ensure LEDs match initial state
  menuLedsUpdate(state);

  for (;;) {
    const uint32_t now = millis();

    // Consolidated temp-mute safety — one check per cycle
    checkTempMuteRestore();
    
    UiEvent ev;
    const bool gotEv = (xQueueReceive(uiQueue, &ev, pdMS_TO_TICKS(100)) == pdTRUE);

    oledHousekeeping(state);
    bootHoldMaybeClear();

    // ---------------------------
    // Housekeeping (runs even if no events arrive)
    // ---------------------------
    if (!gotEv) {
      // Global timeout enforcement for menu-ish states
      if (state == SCREEN_MENU_ENGINE || state == SCREEN_INFO || state == SCREEN_CONFIRM || state == SCREEN_WIFI_STATUS || state == SCREEN_LIST) {
        if (!(g_infoMode == INFO_FOTA_STATUS && g_fotaBusy)) {
          if (menuTimedOut()) {
            exitToIdleFromMenu(state);
            menuLedsUpdate(state);          // <-- keep LEDs in sync after exit
            lastIdleRedrawMs = millis();
          }
        }
      }
      // ADD THIS SAFETY CHECK: consolidated temp-mute already handled above

      const uint32_t now = millis();

      if (state == SCREEN_ARC_IDLE) {
        // ARC IDLE: hybrid redraw policy. In housekeeping we keep a low-rate refresh.
        const uint32_t periodMs = 1000; // 1 Hz
        if (g_oledIsOn && (now - lastIdleRedrawMs >= periodMs)) {
          lastIdleRedrawMs = now;
          // In the UI task housekeeping section
          if (state == SCREEN_ARC_IDLE) {
            const uint32_t now = millis();
            
            // Only draw playback screen if DFPlayer is actually playing AND powered
            if (g_dfIsPlaying && g_dfPowered) {
              // Draw playback screen logic
              if (now - lastIdleRedrawMs >= periodMs) {
                lastIdleRedrawMs = now;
                ui_drawPlaybackScreen();
              }
            } else {
              // Draw arc idle screen
              if (now - lastIdleRedrawMs >= periodMs) {
                lastIdleRedrawMs = now;
                ui_drawArcIdleScreen();
              }
            }
          }
        }
      } else if (state == SCREEN_PRAYER_SCREEN) {
        // Prayer Screen: auto-return to ARC IDLE after 15s
        if (g_prayerScreenUntilMs != 0 && (int32_t)(now - g_prayerScreenUntilMs) >= 0) {
          g_prayerScreenUntilMs = 0;
          state = SCREEN_ARC_IDLE;
          ledsAllOff();
          lastIdleRedrawMs = now;
          ui_drawArcIdleScreen();
        }
      } else if (state == SCREEN_WIFI_AP) {
        if (now - lastApRedrawMs >= 1000) {
          lastApRedrawMs = now;
          ui_drawWifiApInstructions();
        }
      } else if (state == SCREEN_WIFI_STATUS) {
        if (now - lastWifiStatusRedrawMs >= 1000) {
          lastWifiStatusRedrawMs = now;

          xSemaphoreTake(g_displayMtx, portMAX_DELAY);
          display.clearDisplay();
          display.setTextColor(SH110X_WHITE);
          display.setFont(nullptr);
          display.setTextSize(1);

          drawCentered("WiFi Status", 0, 1);

          if (WiFi.status() == WL_CONNECTED) {
            String ssid = WiFi.SSID();
            int rssi = WiFi.RSSI();
            String ip = WiFi.localIP().toString();

            display.setCursor(0, 16);
            display.print("SSID: ");
            display.print(ssid);

            display.setCursor(0, 30);
            display.print("RSSI: ");
            display.print(rssi);
            display.print(" dBm");

            display.setCursor(0, 44);
            display.print("IP: ");
            display.print(ip);

            drawCentered("Press any button", 56, 1);
          } else {
            drawCentered("Disconnected", 28, 1);
            drawCentered("Press any button", 50, 1);
          }

          display.display();
          xSemaphoreGive(g_displayMtx);
        }
      } else if (state == SCREEN_INFO) {
        if (now - lastInfoRedrawMs >= 1000) {
          lastInfoRedrawMs = now;

          if (g_infoMode == INFO_FOTA_STATUS) {
            drawFotaStatusPage();
          } else if (g_infoMode == INFO_CURRENT_SETTINGS) {
            drawCurrentSettingsPage();
          } else if (g_infoMode == INFO_TEXT) {
            drawInfoPage(g_infoTitle);
          } else if (g_infoMode == INFO_ABOUT) {
            drawAboutPage();
          } else if (g_infoMode == INFO_QIBLA) {
            drawQiblaFinderPage();
          }
        }
      }

      continue;
    }

    // ---------------------------
    // Event processing
    // ---------------------------
    // NEW: Handle WiFi connecting event (clears AP screen immediately)
    if (ev.type == UI_EVT_WIFI_STA_CONNECTING) {
      LOGI(LOG_TAG_UI, "WiFi connecting - switching to splash");
      state = SCREEN_SPLASH;
      ledsAllOff();
      ui_drawSplash();
      xTimerStart(splashTimer, 0);
      continue;
    }
    
    if (ev.type == UI_EVT_WIFI_AP_STARTED) {
      state = SCREEN_WIFI_AP;
      g_apOnboardPage = 0;                       // reset to first page
      menuLedsUpdate(state);                 // <-- LEDs ON in AP screen (menu-like)
      ui_drawWifiApInstructions();
      lastApRedrawMs = millis();
      continue;
    }

    if (ev.type == UI_EVT_DF_BUSY_CHANGED) {
      const uint8_t lvl = g_dfBusyLevel;
      const bool playing = (lvl == LOW); // active-low

      static bool s_lastPlaying = false;
      if (playing != s_lastPlaying) {
        s_lastPlaying = playing;
        g_dfIsPlaying = playing;

        if (playing) {
          g_dfStarting = false;
          LOGI(LOG_TAG_ADHAN, "DF_BUSY changed: level=%u -> playing=1 (dt=%lu ms)",
              (unsigned)lvl,
              (unsigned long)(millis() - g_dfPlayCmdAtMs));
        } else {
          LOGI(LOG_TAG_ADHAN, "DF_BUSY changed: level=%u -> playing=0", (unsigned)lvl);

          // Playback ended -> restore temporary mute if active
          if (g_tempMuteActive) {
            restoreVolumeAfterTempMute();
          }
          // Playback ended -> power DF down soon (unless something else extends it)
          dfScheduleOff(2000);
        }

        forceIdleRedraw = true;
      }
    }


    switch (state) {
      case SCREEN_SPLASH:
                // Factory reset check (MUST BE FIRST!)
        if (ev.type == UI_EVT_FACTORY_RESET_TRIGGER) {
          LOGI(LOG_TAG_UI, "Factory reset triggered from idle screen");
          g_factoryResetActive = true;
          g_factoryResetStartMs = millis();
          state = SCREEN_FACTORY_RESET;
          oledWakeFor(OLED_ON_MS_MENU);
          ui_drawFactoryResetScreen();
          break;  // Important: exit this case immediately
        }
        if (ev.type == UI_EVT_TICK) {
          if (g_splashStatusChanged) {
            g_splashStatusChanged = false;
            ui_drawSplash();
          }
        }
        if (ev.type == UI_EVT_SPLASH_DONE) {
          state = SCREEN_ARC_IDLE;
          ledsAllOff();
          oledWakeFor(8000);
          ui_drawArcIdleScreen();
          lastIdleRedrawMs = millis();
        }
        break;

      
      case SCREEN_WIFI_AP:
        // Factory reset check (MUST BE FIRST!)
        if (ev.type == UI_EVT_FACTORY_RESET_TRIGGER) {
          LOGI(LOG_TAG_UI, "Factory reset triggered from idle screen");
          g_factoryResetActive = true;
          g_factoryResetStartMs = millis();
          state = SCREEN_FACTORY_RESET;
          oledWakeFor(OLED_ON_MS_MENU);
          ui_drawFactoryResetScreen();
          break;  // Important: exit this case immediately
        }
        if (ev.type == UI_EVT_WIFI_STA_CONNECTED) {
          state = SCREEN_ARC_IDLE;
          ledsAllOff();
          if (g_oledIsOn) {
            if (g_dfIsPlaying) ui_drawPlaybackScreen();
            else               ui_drawArcIdleScreen();
          }
          lastIdleRedrawMs = millis();
        } else if (ev.type == UI_EVT_BTN_DN) {
          // Scroll down through onboarding pages
          if (g_apOnboardPage < AP_ONBOARD_PAGES - 1) {
            g_apOnboardPage++;
            ui_drawWifiApInstructions();
            lastApRedrawMs = millis();
          }
        } else if (ev.type == UI_EVT_BTN_UP) {
          // Scroll up through onboarding pages
          if (g_apOnboardPage > 0) {
            g_apOnboardPage--;
            ui_drawWifiApInstructions();
            lastApRedrawMs = millis();
          }
        } else if (ev.type == UI_EVT_BTN_OK || ev.type == UI_EVT_BTN_OK_LONG) {
          // Don't exit AP screen on OK button - user must connect through portal
          // Just refresh the screen instead
          ui_drawWifiApInstructions();
          lastApRedrawMs = millis();
        } else if (ev.type == UI_EVT_TICK) {
          ui_drawWifiApInstructions();
          lastApRedrawMs = millis();
        }
        break;

      case SCREEN_ARC_IDLE:
        // Factory reset check (MUST BE FIRST!)
        if (ev.type == UI_EVT_FACTORY_RESET_TRIGGER) {
          LOGI(LOG_TAG_UI, "Factory reset triggered from idle screen");
          g_factoryResetActive = true;
          g_factoryResetStartMs = millis();
          state = SCREEN_FACTORY_RESET;
          oledWakeFor(OLED_ON_MS_MENU);
          ui_drawFactoryResetScreen();
          break;  // Important: exit this case immediately
        }
        if (ev.type == UI_EVT_BTN_UP) {
          noteUiAction();
          pulseLeds(0x01 /*BTN*/, 900);
          volumeInc();
          oledWakeFor(OLED_ON_MS_VOL);
          if (g_oledIsOn) {
            if (g_dfIsPlaying) ui_drawPlaybackScreen();
            else               ui_drawArcIdleScreen();
          }
          lastIdleRedrawMs = millis();
        }
        else if (ev.type == UI_EVT_BTN_DN) {
          noteUiAction();
          pulseLeds(0x01 /*BTN*/, 900);
          volumeDec();
          oledWakeFor(OLED_ON_MS_VOL);
          if (g_oledIsOn) {
            if (g_dfIsPlaying) ui_drawPlaybackScreen();
            else               ui_drawArcIdleScreen();
          }
          lastIdleRedrawMs = millis();
        }
        else if (ev.type == UI_EVT_BTN_OK) {
          noteUiAction();

          // ARC IDLE -> PRAYER SCREEN
          state = SCREEN_PRAYER_SCREEN;
          g_prayerScreenUntilMs = millis() + 15000;

          // LEDs: OK LED ON steady, BTN LED OFF
          okLedOn(true);
          btnLedOn(false);

          if (g_oledIsOn) ui_drawPrayerScreen();
        }
        else if (ev.type == UI_EVT_BTN_OK_LONG) {
          // ADD THIS: Handle long press during playback
          if (g_dfIsPlaying) {
            noteUiAction();
            instantMute();
            if (g_oledIsOn) ui_drawPlaybackScreen();
            lastIdleRedrawMs = millis();
          }
          // No action in ARC IDLE when not playing (per spec)
        }
        else if (ev.type == UI_EVT_TICK ||
                 ev.type == UI_EVT_WIFI_STA_CONNECTED ||
                 ev.type == UI_EVT_LOCATION_READY ||
                 ev.type == UI_EVT_PRAYER_READY ||
                 ev.type == UI_EVT_DF_BUSY_CHANGED) {

          if (ev.type == UI_EVT_PRAYER_READY) {
            arcIdleApplyPrefs();
            arcIdleUpdateFromToday();
            forceIdleRedraw = true;
          }

          if (g_oledIsOn) {
            if (g_dfIsPlaying) ui_drawPlaybackScreen();
            else               ui_drawArcIdleScreen();
          }
          lastIdleRedrawMs = millis();
        }
        break;

      case SCREEN_PRAYER_SCREEN:
        // Factory reset check (MUST BE FIRST!)
        if (ev.type == UI_EVT_FACTORY_RESET_TRIGGER) {
          LOGI(LOG_TAG_UI, "Factory reset triggered from idle screen");
          g_factoryResetActive = true;
          g_factoryResetStartMs = millis();
          state = SCREEN_FACTORY_RESET;
          oledWakeFor(OLED_ON_MS_MENU);
          ui_drawFactoryResetScreen();
          break;  // Important: exit this case immediately
        }
        if (ev.type == UI_EVT_BTN_UP) {
          noteUiAction();
          pulseLeds(0x01 /*BTN*/, 900);
          volumeInc();
          if (g_oledIsOn) ui_drawPrayerScreen();
        }
        else if (ev.type == UI_EVT_BTN_DN) {
          noteUiAction();
          pulseLeds(0x01 /*BTN*/, 900);
          volumeDec();
          if (g_oledIsOn) ui_drawPrayerScreen();
        }
        else if (ev.type == UI_EVT_BTN_OK) {
          noteUiAction();
          // PRAYER SCREEN -> MENUS
          g_prayerScreenUntilMs = 0;

          oledWakeFor(OLED_ON_MS_MENU);
          menuPush(&PAGE_ROOT);
          state = SCREEN_MENU_ENGINE;
          menuLedsUpdate(state); // both LEDs ON in menus
          drawMenuPage();
        }
        else if (ev.type == UI_EVT_BTN_OK_LONG) {
          // ADD THIS: Handle long press during playback (if user happens to be in prayer screen while playing)
          if (g_dfIsPlaying) {
            noteUiAction();
            instantMute();
            if (g_oledIsOn) ui_drawPlaybackScreen(); // or ui_drawPrayerScreen() depending on what you want
            lastIdleRedrawMs = millis();
          }
        }
        else if (ev.type == UI_EVT_TICK) {
          // Keep screen fresh at 1 Hz if needed (volume emphasis box)
          if (g_oledIsOn) ui_drawPrayerScreen();
        }
        // Long OK during prayer screen: no action (mute remains playback-only / menu behavior)
        break;

      case SCREEN_MENU_ENGINE: {
        // Factory reset check (MUST BE FIRST!)
        if (ev.type == UI_EVT_FACTORY_RESET_TRIGGER) {
          LOGI(LOG_TAG_UI, "Factory reset triggered from idle screen");
          g_factoryResetActive = true;
          g_factoryResetStartMs = millis();
          state = SCREEN_FACTORY_RESET;
          oledWakeFor(OLED_ON_MS_MENU);
          ui_drawFactoryResetScreen();
          break;  // Important: exit this case immediately
        }
        MenuCtx& c = menuCur();

        if (ev.type == UI_EVT_BTN_DN) {
          noteMenuAction();
          c.sel = (c.sel + 1) % c.page->count;
          drawMenuPage();
        }
        else if (ev.type == UI_EVT_BTN_UP) {
          noteMenuAction();
          c.sel = (c.sel - 1);
          if (c.sel < 0) c.sel = c.page->count - 1;
          drawMenuPage();
        }
        else if (ev.type == UI_EVT_BTN_OK_LONG) {
          noteMenuAction();
          instantMute();
          drawMenuPage();
        }
        else if (ev.type == UI_EVT_BTN_OK) {
          noteMenuAction();
          const MenuItem& it = c.page->items[c.sel];

          // --- Special screens / actions by label ---
          if (strcmp(it.label, "Wi-Fi Status") == 0) {
            state = SCREEN_WIFI_STATUS;
            menuLedsUpdate(state);
            lastWifiStatusRedrawMs = 0;
            break;
          }

          if (strcmp(it.label, "Qibla Direction") == 0) {
            g_infoMode = INFO_QIBLA;
            state = SCREEN_INFO;
            menuLedsUpdate(state);
            drawQiblaFinderPage();
            lastInfoRedrawMs = millis();
            break;
          }

          if (strcmp(it.label, "Firmware Update") == 0) {
            // User has acknowledged the update; stop showing idle banner
            g_idleUpdateBanner = false;
            forceIdleRedraw = true;   // so idle top row returns to normal immediately on exit

            g_infoMode = INFO_FOTA_STATUS;
            state = SCREEN_INFO;
            menuLedsUpdate(state);
            drawFotaStatusPage();
            lastInfoRedrawMs = millis();

            if (it.action) it.action();
            break;
          }

          if (strcmp(it.label, "Time Zone") == 0) {
            startList(LIST_TZ);
            state = SCREEN_LIST;
            menuLedsUpdate(state);
            drawListPage();
            break;
          }

          if (strcmp(it.label, "Method") == 0) {
            startList(LIST_METHOD);
            state = SCREEN_LIST;
            menuLedsUpdate(state);
            drawListPage();
            break;
          }

          if (strcmp(it.label, "School") == 0) {
            startList(LIST_SCHOOL);
            state = SCREEN_LIST;
            menuLedsUpdate(state);
            drawListPage();
            break;
          }

          if (strcmp(it.label, "Angle Method") == 0) {
            startList(LIST_LATADJ);
            state = SCREEN_LIST;
            menuLedsUpdate(state);
            drawListPage();
            break;
          }

          if (strcmp(it.label, "About") == 0) {
            g_infoMode = INFO_ABOUT;
            state = SCREEN_INFO;
            menuLedsUpdate(state);
            drawAboutPage();
            lastInfoRedrawMs = millis();
            break;
          }

          // ---- Default menu engine behaviour ----
          if (it.type == MI_SUBMENU && it.submenu) {
            menuPush(it.submenu);
            drawMenuPage();

          } else if (it.type == MI_BACK) {
            if (g_menuDepth <= 0) {
              exitToIdleFromMenu(state);
              menuLedsUpdate(state);
              lastIdleRedrawMs = millis();
            } else {
              menuPop();
              drawMenuPage();
            }

          } else if (it.type == MI_INFO) {
            g_infoMode = INFO_TEXT;
            g_infoTitle = it.infoTitle ? it.infoTitle : it.label;
            state = SCREEN_INFO;
            menuLedsUpdate(state);
            drawInfoPage(g_infoTitle);
            lastInfoRedrawMs = millis();

          } else if (it.type == MI_CONFIRM) {
            confirmStart(it.label, it.action);
            state = SCREEN_CONFIRM;
            menuLedsUpdate(state);

          } else if (it.type == MI_ACTION && it.action) {
            it.action();
            drawMenuPage();
          }
        }
        else if (ev.type == UI_EVT_TICK) {
          if (menuTimedOut()) {
            exitToIdleFromMenu(state);
            menuLedsUpdate(state);
            lastIdleRedrawMs = millis();
          }
        }
      } break;

      case SCREEN_LIST: {
        // Factory reset check (MUST BE FIRST!)
        if (ev.type == UI_EVT_FACTORY_RESET_TRIGGER) {
          LOGI(LOG_TAG_UI, "Factory reset triggered from idle screen");
          g_factoryResetActive = true;
          g_factoryResetStartMs = millis();
          state = SCREEN_FACTORY_RESET;
          oledWakeFor(OLED_ON_MS_MENU);
          ui_drawFactoryResetScreen();
          break;  // Important: exit this case immediately
        }
        if (ev.type == UI_EVT_BTN_DN) {
          noteMenuAction();
          g_listSel = (g_listSel + 1) % listCount(g_listKind);
          drawListPage();
          break;
        }
        if (ev.type == UI_EVT_BTN_UP) {
          noteMenuAction();
          g_listSel--;
          if (g_listSel < 0) g_listSel = listCount(g_listKind) - 1;
          drawListPage();
          break;
        }

        if (ev.type == UI_EVT_BTN_OK_LONG) {
          noteMenuAction();
          instantMute();
          drawListPage();
          break;
        }

        if (ev.type == UI_EVT_BTN_OK) {
          noteMenuAction();

          if (listIsBack(g_listKind, g_listSel)) {
            state = SCREEN_MENU_ENGINE;
            menuLedsUpdate(state);
            drawMenuPage();
            break;
          }

          bool changed = false;

          if (g_listKind == LIST_TZ) {
            String newTz = listValueTz(g_listSel);  // "" = Auto
            if (newTz != g_tzOverride) {
              g_tzOverride = newTz;
              markSettingsDirty();
              changed = true;

              if (g_tzOverride.length()) applyTimezonePosix(g_tzOverride);
              else g_tzConfigured = false;
            }
          }
          else if (g_listKind == LIST_METHOD) {
            int v = listValueInt(LIST_METHOD, g_listSel);
            if (v != g_method) { g_method = v; markSettingsDirty(); changed = true; }
          }
          else if (g_listKind == LIST_SCHOOL) {
            int v = listValueInt(LIST_SCHOOL, g_listSel);
            if (v != g_school) { g_school = v; markSettingsDirty(); changed = true; }
          }
          else if (g_listKind == LIST_LATADJ) {
            int v = listValueInt(LIST_LATADJ, g_listSel);
            if (v != g_latAdj) { g_latAdj = v; markSettingsDirty(); changed = true; }
          }

          if (changed) {
            xSemaphoreTake(g_dataMtx, portMAX_DELAY);
            g_prayerReady = false;
            xSemaphoreGive(g_dataMtx);

            WifiCmd cmd{ WIFI_CMD_RUN_DAILY_REFRESH };
            xQueueSend(wifiCmdQueue, &cmd, 0);
          }

          state = SCREEN_MENU_ENGINE;
          menuLedsUpdate(state);
          drawMenuPage();
          break;
        }

        if (ev.type == UI_EVT_TICK) {
          // Force redraw on minute change to update countdown
          static int lastMinute = -1;
          struct tm tmNow;
          if (getLocalTime(&tmNow, 0)) {
            if (tmNow.tm_min != lastMinute) {
              lastMinute = tmNow.tm_min;
              forceIdleRedraw = true;
            }
          }
          if (menuTimedOut()) {
            exitToIdleFromMenu(state);
            menuLedsUpdate(state);
            lastIdleRedrawMs = millis();
          }
        }
      } break;

      case SCREEN_FACTORY_RESET:
        if (ev.type == UI_EVT_FACTORY_RESET_CANCEL) {
          // User released button - cancel reset
          LOGI(LOG_TAG_UI, "Factory reset cancelled");
          g_factoryResetActive = false;
          
          // Return to idle screen
          state = SCREEN_ARC_IDLE;
          oledWakeFor(OLED_ON_MS_MENU);
          if (g_oledIsOn) {
            if (g_dfIsPlaying) ui_drawPlaybackScreen();
            else               ui_drawArcIdleScreen();
          }
          lastIdleRedrawMs = millis();
          
        } else if (ev.type == UI_EVT_TICK) {
          // Update countdown display every second
          ui_drawFactoryResetScreen();
          
          // Check if countdown completed
          uint32_t elapsed = millis() - g_factoryResetStartMs;
          if (elapsed >= FACTORY_RESET_COUNTDOWN_MS) {
            // Countdown complete - perform reset
            LOGI(LOG_TAG_UI, "Factory reset countdown complete - performing reset");
            performFactoryReset();  // This will restart the device
          }
        }
        break;

      case SCREEN_INFO:
        // Factory reset check (MUST BE FIRST!)
        if (ev.type == UI_EVT_FACTORY_RESET_TRIGGER) {
          LOGI(LOG_TAG_UI, "Factory reset triggered from idle screen");
          g_factoryResetActive = true;
          g_factoryResetStartMs = millis();
          state = SCREEN_FACTORY_RESET;
          oledWakeFor(OLED_ON_MS_MENU);
          ui_drawFactoryResetScreen();
          break;  // Important: exit this case immediately
        }
        if (ev.type == UI_EVT_BTN_OK_LONG) {
          noteMenuAction();
          instantMute();

          if (g_infoMode == INFO_FOTA_STATUS) drawFotaStatusPage();
          else if (g_infoMode == INFO_CURRENT_SETTINGS) drawCurrentSettingsPage();
          else if (g_infoMode == INFO_TEXT) drawInfoPage(g_infoTitle);
          else if (g_infoMode == INFO_ABOUT) drawAboutPage();
          else if (g_infoMode == INFO_QIBLA) drawQiblaFinderPage();

          lastInfoRedrawMs = millis();
          break;
        }

        if (g_infoMode == INFO_FOTA_STATUS && g_fotaBusy) {
          if (ev.type == UI_EVT_BTN_UP || ev.type == UI_EVT_BTN_DN || ev.type == UI_EVT_BTN_OK) {
            noteMenuAction();
            break;
          }
        }

        if (ev.type == UI_EVT_BTN_UP || ev.type == UI_EVT_BTN_DN || ev.type == UI_EVT_BTN_OK) {
          noteMenuAction();
          state = SCREEN_MENU_ENGINE;
          menuLedsUpdate(state);
          g_infoMode = INFO_NONE;
          drawMenuPage();

        } else if (ev.type == UI_EVT_TICK) {
          if (menuTimedOut()) {
            if (!(g_infoMode == INFO_FOTA_STATUS && g_fotaBusy)) {
              exitToIdleFromMenu(state);
              menuLedsUpdate(state);
              lastIdleRedrawMs = millis();
            }
            break;
          }

          if (g_infoMode == INFO_CURRENT_SETTINGS) {
            drawCurrentSettingsPage();
            lastInfoRedrawMs = millis();
          } else if (g_infoMode == INFO_TEXT) {
            drawInfoPage(g_infoTitle);
            lastInfoRedrawMs = millis();
          } else if (g_infoMode == INFO_FOTA_STATUS) {
            drawFotaStatusPage();
            lastInfoRedrawMs = millis();
          } else if (g_infoMode == INFO_ABOUT) {
            drawAboutPage();
            lastInfoRedrawMs = millis();
          } else if (g_infoMode == INFO_QIBLA) {
            drawQiblaFinderPage();
            lastInfoRedrawMs = millis();
          }
        }
        break;

      case SCREEN_CONFIRM:
        // Factory reset check (MUST BE FIRST!)
        if (ev.type == UI_EVT_FACTORY_RESET_TRIGGER) {
          LOGI(LOG_TAG_UI, "Factory reset triggered from idle screen");
          g_factoryResetActive = true;
          g_factoryResetStartMs = millis();
          state = SCREEN_FACTORY_RESET;
          oledWakeFor(OLED_ON_MS_MENU);
          ui_drawFactoryResetScreen();
          break;  // Important: exit this case immediately
        }
        if (ev.type == UI_EVT_BTN_OK_LONG) {
          noteMenuAction();
          instantMute();
          drawConfirmPage(g_confirmTitle);
          break;
        }

        if (ev.type == UI_EVT_BTN_UP || ev.type == UI_EVT_BTN_DN) {
          noteMenuAction();
          g_confirmSel = (g_confirmSel + 1) % 2;
          drawConfirmPage(g_confirmTitle);

        } else if (ev.type == UI_EVT_BTN_OK) {
          noteMenuAction();
          if (g_confirmSel == 0) {
            if (g_confirmProceed) g_confirmProceed();
            exitToIdleFromMenu(state);
            menuLedsUpdate(state);
            lastIdleRedrawMs = millis();
          } else {
            state = SCREEN_MENU_ENGINE;
            menuLedsUpdate(state);
            drawMenuPage();
          }

        } else if (ev.type == UI_EVT_TICK) {
          if (menuTimedOut()) {
            exitToIdleFromMenu(state);
            menuLedsUpdate(state);
            lastIdleRedrawMs = millis();
          }
        }
        break;

      case SCREEN_WIFI_STATUS:
        // Factory reset check (MUST BE FIRST!)
        if (ev.type == UI_EVT_FACTORY_RESET_TRIGGER) {
          LOGI(LOG_TAG_UI, "Factory reset triggered from idle screen");
          g_factoryResetActive = true;
          g_factoryResetStartMs = millis();
          state = SCREEN_FACTORY_RESET;
          oledWakeFor(OLED_ON_MS_MENU);
          ui_drawFactoryResetScreen();
          break;  // Important: exit this case immediately
        }
        if (ev.type == UI_EVT_BTN_OK_LONG) {
          noteMenuAction();
          instantMute();
          lastWifiStatusRedrawMs = 0;
          break;
        }

        if (ev.type == UI_EVT_BTN_OK || ev.type == UI_EVT_BTN_UP || ev.type == UI_EVT_BTN_DN) {
          noteMenuAction();
          state = SCREEN_MENU_ENGINE;
          menuLedsUpdate(state);
          drawMenuPage();

        } else if (ev.type == UI_EVT_TICK) {
          if (menuTimedOut()) {
            exitToIdleFromMenu(state);
            menuLedsUpdate(state);
            lastIdleRedrawMs = millis();
            break;
          }
          lastWifiStatusRedrawMs = 0;
        }
        break;

      default:
        // No periodic redraw here; each screen manages its own refresh.
        break;
    }
  }
}

// ============================================================================
// Prayer Chime Task
// ============================================================================
void prayerChimeTask(void*) {
  int lastChimeMinute = -1;
  PrayerId lastChimed = PR_NONE;
  uint32_t lastChimeSentMs = 0;
  const uint32_t minSpacingMs = 30UL * 1000UL;

  // How long to keep DF powered after starting a track (tweak as needed)
  const uint32_t DF_OFF_AFTER_MS_FULL  = 240UL * 1000UL; // full adhan can be long
  const uint32_t DF_OFF_AFTER_MS_SHORT = 45UL  * 1000UL; // short
  const uint32_t DF_OFF_AFTER_MS_CHIME = 20UL  * 1000UL; // chime

  auto pickAtMinute = [&](const PrayerTimesDay& t, int curMin) -> PrayerId {
    if (!t.valid()) return PR_NONE;
    if (curMin == t.fajr)    return PR_FAJR;
    if (curMin == t.dhuhr)   return PR_DHUHR;
    if (curMin == t.asr)     return PR_ASR;
    if (curMin == t.maghrib) return PR_MAGHRIB;
    if (curMin == t.isha)    return PR_ISHA;
    return PR_NONE;
  };

  for (;;) {
    // Wake at ~1 Hz
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Snapshot prayer tables
    PrayerTimesDay td, tm;
    bool ready;
    xSemaphoreTake(g_dataMtx, portMAX_DELAY);
    ready = g_prayerReady;
    td = g_today;
    tm = g_tomorrow;
    xSemaphoreGive(g_dataMtx);
    if (!ready || !td.valid() || !tm.valid()) continue;

    // Read local time
    struct tm lt;
    if (!getLocalTime(&lt, 0)) continue;

    // Only evaluate at the start of a minute
    if (lt.tm_sec != 0) continue;

    const int curMin = lt.tm_hour * 60 + lt.tm_min;

    PrayerId target = PR_NONE;
    if (curMin <= td.isha) target = pickAtMinute(td, curMin);
    else                   target = pickAtMinute(tm, curMin);

    if (target == PR_NONE) continue;

    // De-dupe: same minute & same prayer -> do nothing
    if (curMin == lastChimeMinute && target == lastChimed) continue;

    // Record de-dupe state early (prevents retrigger within same minute)
    lastChimeMinute = curMin;
    lastChimed = target;

    // Mute? (do nothing, and keep DF off)
    if (g_currentVolIdx == 0) {
      LOGI(LOG_TAG_ADHAN, "Adhan suppressed (MUTE) for %s", PRAYER_NAME[(uint8_t)target]);
      continue;
    }

    // Spacing guard (extra safety for time jumps / task restarts)
    uint32_t nowMs = millis();
    if ((nowMs - lastChimeSentMs) < minSpacingMs) continue;

    // Choose track + DF off delay
    uint8_t track;
    uint32_t offDelayMs;

    if (g_adhanType == 0) {            // FULL
      track = (target == PR_FAJR) ? 1 : 2;
      offDelayMs = DF_OFF_AFTER_MS_FULL;
    } else if (g_adhanType == 1) {     // SHORT
      track = 3;
      offDelayMs = DF_OFF_AFTER_MS_SHORT;
    } else {                           // CHIME
      track = 4;
      offDelayMs = DF_OFF_AFTER_MS_CHIME;
    }

    // ---- Power on DFPlayer only now ----
    dfPowerOn();

    if (!g_dfOk) {
      LOGW(LOG_TAG_ADHAN, "DF not ready after power-on; skipping playback");
      // Ensure we don't leave it on if init failed
      dfScheduleOff(1500);
      continue;
    }

    // Ensure DF volume matches current setting
    dfSetVolumeSafe((uint8_t)VOL_LEVELS[g_currentVolIdx]);

    if (g_tempMuteActive) {
      restoreVolumeAfterTempMute();
    }

    LOGI(LOG_TAG_ADHAN,
         "Adhan playing: %s (type=%u) folder=%u track=%u vol=%s",
         PRAYER_NAME[(uint8_t)target],
         (unsigned)g_adhanType,
         1u,
         (unsigned)track,
         VOL_NAMES[g_currentVolIdx]);

    oledWakeFor(offDelayMs + 5000);

    // Start playback
    g_playbackPrayerId = target;
    forceIdleRedraw = true;
    dfPlayFolderSafe(1, track);
    lastChimeSentMs = nowMs;

    // Schedule DF power off after track should be done
    dfScheduleOff(offDelayMs);
  }
}

void arcIdleApplyPrefs() {
  ArcUserPrefs p;  // Changed from ArcIdleRenderer::Prefs
  p.is24h = (g_timeFormat == 1);
  g_arcIdle.setPrefs(p);
}

void arcIdleUpdateFromToday() {
  if (g_prayerReady && g_today.valid()) {
    ArcPrayerTimes pt;  // Changed from ArcIdleRenderer::PrayerTimes
    pt.fajr    = g_today.fajr;
    pt.sunrise = g_today.sunrise;
    pt.dhuhr   = g_today.dhuhr;
    pt.asr     = g_today.asr;
    pt.maghrib = g_today.maghrib;
    pt.isha    = g_today.isha;
    g_arcIdle.setPrayerTimes(pt);
  }
}
