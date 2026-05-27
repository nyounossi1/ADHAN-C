/*
 * DFPlayerManager.cpp — DFPlayer UART, raw frame playback, volume, power.
 *
 * AD-33: replaced DFRobotDFPlayerMini library with raw 10-byte serial frames.
 *
 * Hardware note (01b → 02c rework):
 *   U13 (TPS22913C, 250 mA) is bypassed — DFPlayer VCC is permanently wired
 *   to +5V. IO13 (DF_ON_PIN) no longer controls power. dfPowerOn/Off now
 *   manage only the UART interface, not the power rail.
 *
 * Frame format (TX and RX):
 *   7E FF 06 CMD ACK P1 P2 CHK_H CHK_L EF
 *   Checksum = -(0xFF + 0x06 + CMD + ACK + P1 + P2)  [int16, big-endian]
 *
 * Key commands:
 *   0x06  set volume  (P2 = 0–30)
 *   0x0F  play folder (P1 = folder 01-99, P2 = track 001-255)
 *   0x16  stop
 *   0x48  query SD file count
 *
 * Key responses:
 *   0x3F  startup — SD online when (P2 & 0x02)
 *   0x3D  track finished
 *   0x41  ACK
 *   0x40  error (P2: 1=busy, 5=no such index, 6=file not found)
 */
#include "DFPlayerManager.h"

static SemaphoreHandle_t g_dfMtx   = nullptr;
TimerHandle_t    dfOffTimer         = nullptr;
SemaphoreHandle_t g_dfOffMtx        = nullptr;
static uint32_t  g_dfOffAtMs        = 0;

// ---------------------------------------------------------------------------
// Raw frame state
// ---------------------------------------------------------------------------
static uint8_t s_rxBuf[10];
static uint8_t s_rxIdx   = 0;
static uint8_t s_lastCmd = 0;
static uint8_t s_lastP1  = 0;
static uint8_t s_lastP2  = 0;

// ---------------------------------------------------------------------------
// Mutex helpers
// ---------------------------------------------------------------------------
static inline bool dfTake(uint32_t ms) {
  if (!g_dfMtx) return true;
  return xSemaphoreTake(g_dfMtx, pdMS_TO_TICKS(ms)) == pdTRUE;
}
static inline void dfGive() { if (g_dfMtx) xSemaphoreGive(g_dfMtx); }

// ---------------------------------------------------------------------------
// Raw frame helpers
// ---------------------------------------------------------------------------
static void logFrame(const char* dir, const uint8_t* f) {
  LOGI(LOG_TAG_ADHAN,
       "DF %s: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
       dir, f[0],f[1],f[2],f[3],f[4],f[5],f[6],f[7],f[8],f[9]);
}

static void sendCmd(uint8_t cmd, uint8_t p1, uint8_t p2) {
  uint8_t f[10] = { 0x7E, 0xFF, 0x06, cmd, 0x01, p1, p2, 0x00, 0x00, 0xEF };
  int16_t sum   = -(int16_t)(0xFF + 0x06 + cmd + 0x01 + p1 + p2);
  f[7] = (uint8_t)((sum >> 8) & 0xFF);
  f[8] = (uint8_t)(sum & 0xFF);
  logFrame("TX", f);
  Serial2.write(f, 10);
}

// Consume all available RX bytes; assemble complete frames.
// Returns true if at least one frame was decoded.
static bool drainRx() {
  bool got = false;
  while (Serial2.available()) {
    uint8_t b = (uint8_t)Serial2.read();
    if (s_rxIdx == 0 && b != 0x7E) continue; // wait for start byte
    s_rxBuf[s_rxIdx++] = b;
    if (s_rxIdx < 10) continue;
    s_rxIdx = 0;
    if (s_rxBuf[9] != 0xEF) continue;        // bad end byte — skip
    s_lastCmd = s_rxBuf[3];
    s_lastP1  = s_rxBuf[5];
    s_lastP2  = s_rxBuf[6];
    logFrame("RX", s_rxBuf);
    if (s_lastCmd == 0x40) {
      LOGW(LOG_TAG_ADHAN, "DF error: code=0x%02X (%s)", s_lastP2,
           s_lastP2 == 1 ? "busy" :
           s_lastP2 == 5 ? "no index" :
           s_lastP2 == 6 ? "file not found" : "unknown");
    }
    got = true;
  }
  return got;
}

// Send a command then drain RX for waitMs to avoid back-to-back collisions.
static void sendCmdAndWait(uint8_t cmd, uint8_t p1, uint8_t p2,
                           uint32_t waitMs = 300) {
  sendCmd(cmd, p1, p2);
  uint32_t start = millis();
  while (millis() - start < waitMs) {
    drainRx();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Block until the DFPlayer sends its 0x3F startup frame with SD card online.
// Must be called before any command is sent.
static bool waitForStartup(uint32_t timeoutMs = 5000) {
  LOGI(LOG_TAG_SYS, "DF: waiting for 0x3F startup (timeout=%lu ms)",
       (unsigned long)timeoutMs);
  s_rxIdx = 0;
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    while (Serial2.available()) {
      uint8_t b = (uint8_t)Serial2.read();
      if (s_rxIdx == 0 && b != 0x7E) continue;
      s_rxBuf[s_rxIdx++] = b;
      if (s_rxIdx < 10) continue;
      s_rxIdx = 0;
      if (s_rxBuf[9] != 0xEF) continue;
      logFrame("RX", s_rxBuf);
      if (s_rxBuf[3] == 0x3F) {
        uint8_t p2 = s_rxBuf[6];
        LOGI(LOG_TAG_SYS, "DF: 0x3F startup — SD=%s USB=%s",
             (p2 & 0x02) ? "ONLINE" : "MISSING",
             (p2 & 0x01) ? "ONLINE" : "NO");
        if (p2 & 0x02) return true; // SD card ready
        LOGW(LOG_TAG_SYS, "DF: SD not online in 0x3F — waiting...");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  LOGE(LOG_TAG_SYS, "DF: 0x3F startup timeout — SD not ready");
  return false;
}

// ============================================================================
// Init
// ============================================================================
bool initDFPlayer() {
  if (!g_dfMtx) g_dfMtx = xSemaphoreCreateMutex();
  s_rxIdx = 0;

  LOGI(LOG_TAG_SYS, "DF: Serial2 begin 9600 TX=%d RX=%d", DF_TX_PIN, DF_RX_PIN);
  Serial2.begin(9600, SERIAL_8N1, DF_RX_PIN, DF_TX_PIN);

  if (!waitForStartup(5000)) {
    LOGE(LOG_TAG_SYS, "DF: init failed — check SD card (FAT32, /01/001.mp3)");
    g_dfOk = false;
    return false;
  }

  // Query SD file count for diagnostics (0x48 response carries count in P1:P2)
  s_lastCmd = 0;
  sendCmdAndWait(0x48, 0x00, 0x00, 500);
  if (s_lastCmd == 0x48) {
    uint16_t count = ((uint16_t)s_lastP1 << 8) | s_lastP2;
    LOGI(LOG_TAG_SYS, "DF: SD file count=%u (includes hidden ._files on Mac)",
         (unsigned)count);
  }

  uint8_t vol = (uint8_t)VOL_LEVELS[g_currentVolIdx];
  sendCmdAndWait(0x06, 0x00, vol, 300);
  LOGI(LOG_TAG_SYS, "DF: init OK vol=%u idx=%u BUSY_PIN=%d BUSY=%d",
       (unsigned)vol, (unsigned)g_currentVolIdx,
       DF_BUSY_PIN, digitalRead(DF_BUSY_PIN));

  g_dfOk = true;
  return true;
}

// ============================================================================
// UART safe-off (prevent phantom power via UART TX line)
// ============================================================================
void dfUartSafeOff() {
  Serial2.end();
  pinMode(DF_TX_PIN, INPUT);
  pinMode(DF_RX_PIN, INPUT);
}

// ============================================================================
// Power on / off
// NOTE: U13 (TPS22913C) is hardware-bypassed. DFPlayer has permanent +5V.
//       dfPowerOn/Off manage the UART interface only, not the power rail.
// ============================================================================
void dfPowerOn() {
  if (g_dfPowered) return;
  dfUartSafeOff(); // release pins before re-init
  g_dfPowered = true;
  if (!initDFPlayer()) {
    dfUartSafeOff();
    g_dfPowered = false;
  }
}

void dfPowerOff() {
  if (!g_dfPowered) return;
  if (g_dfOk) sendCmdAndWait(0x16, 0x00, 0x00, 200); // stop playback
  dfUartSafeOff();
  if (g_tempMuteActive) restoreVolumeAfterTempMute();
  g_dfPowered   = false;
  g_dfOk        = false;
  g_dfIsPlaying = false;
  g_dfBusyLevel = 1;
  LOGI(LOG_TAG_SYS, "DF: UART released (U13 bypassed — power always on)");
}

// ============================================================================
// Safe playback / volume (called with dfMtx taken by caller or internally)
// ============================================================================
void dfSetVolumeSafe(uint8_t v) {
  if (!g_dfPowered || !g_dfOk) return;
  if (!dfTake(60)) return;
  LOGI(LOG_TAG_ADHAN, "DF setVol=%u", (unsigned)v);
  sendCmdAndWait(0x06, 0x00, v, 200);
  dfGive();
}

void dfPlayFolderSafe(uint8_t folder, uint8_t track) {
  if (!g_dfPowered || !g_dfOk) return;
  if (!dfTake(120)) return;
  LOGI(LOG_TAG_ADHAN, "DF playFolder folder=%u track=%u", (unsigned)folder, (unsigned)track);
  sendCmdAndWait(0x0F, folder, track, 300); // 0x0F = specify folder
  dfGive();
}

// ============================================================================
// Scheduled power-off timer
// ============================================================================
void dfOffTimerCb(TimerHandle_t) {
  uint32_t now = millis(), offAt = 0;
  if (g_dfOffMtx) xSemaphoreTake(g_dfOffMtx, portMAX_DELAY);
  offAt = g_dfOffAtMs;
  if (g_dfOffMtx) xSemaphoreGive(g_dfOffMtx);

  if (offAt != 0 && (int32_t)(offAt - now) > 0) {
    uint32_t remain = offAt - now;
    xTimerChangePeriod(dfOffTimer, pdMS_TO_TICKS(remain), 0);
    xTimerStart(dfOffTimer, 0);
    return;
  }

  if (g_dfOffMtx) xSemaphoreTake(g_dfOffMtx, portMAX_DELAY);
  g_dfOffAtMs = 0;
  if (g_dfOffMtx) xSemaphoreGive(g_dfOffMtx);

  if (g_tempMuteActive) restoreVolumeAfterTempMute();
  dfPowerOff();
}

void dfScheduleOff(uint32_t ms) {
  if (!dfOffTimer) return;
  const uint32_t now = millis(), newAt = now + ms;
  if (g_dfOffMtx) xSemaphoreTake(g_dfOffMtx, portMAX_DELAY);
  if (g_dfOffAtMs == 0 || (int32_t)(newAt - g_dfOffAtMs) > 0) g_dfOffAtMs = newAt;
  uint32_t remain = (int32_t)(g_dfOffAtMs - now) > 0 ? (g_dfOffAtMs - now) : 1;
  if (g_dfOffMtx) xSemaphoreGive(g_dfOffMtx);
  xTimerStop(dfOffTimer, 0);
  xTimerChangePeriod(dfOffTimer, pdMS_TO_TICKS(remain), 0);
  xTimerStart(dfOffTimer, 0);
}

// ============================================================================
// Volume helpers (UI-facing)
// ============================================================================
static Preferences volPrefs;

void volumeInc() {
  if (g_currentVolIdx < 3) {
    g_currentVolIdx++;
    volPrefs.begin("settings", false);
    volPrefs.putUChar("volIdx", g_currentVolIdx);
    volPrefs.end();
    if (g_dfPowered && g_dfOk) dfSetVolumeSafe((uint8_t)VOL_LEVELS[g_currentVolIdx]);
  }
}

void volumeDec() {
  if (g_currentVolIdx > 0) {
    g_currentVolIdx--;
    volPrefs.begin("settings", false);
    volPrefs.putUChar("volIdx", g_currentVolIdx);
    volPrefs.end();
    if (g_dfPowered && g_dfOk) dfSetVolumeSafe((uint8_t)VOL_LEVELS[g_currentVolIdx]);
  }
}

void instantMute() {
  if (g_dfIsPlaying && g_dfPowered) {
    if (!g_tempMuteActive && g_currentVolIdx != 0) {
      g_tempMutePrevVolIdx = g_currentVolIdx;
      g_currentVolIdx      = 0;
      g_tempMuteActive     = true;
      g_tempMuteStartedMs  = millis();
      if (g_dfOk) dfSetVolumeSafe((uint8_t)VOL_LEVELS[g_currentVolIdx]);
      LOGI(LOG_TAG_UI, "MUTE during playback (prev=%u)", (unsigned)g_tempMutePrevVolIdx);
      forceIdleRedraw = true;
    }
  } else {
    if (g_currentVolIdx != 0) {
      g_prevVolIdx     = g_currentVolIdx;
      g_currentVolIdx  = 0;
      markSettingsDirty();
      if (g_dfPowered && g_dfOk) dfSetVolumeSafe((uint8_t)VOL_LEVELS[g_currentVolIdx]);
      LOGI(LOG_TAG_UI, "MUTE (prev=%u)", (unsigned)g_prevVolIdx);
      forceIdleRedraw = true;
    }
  }
}

void restoreVolumeAfterTempMute() {
  if (g_tempMuteActive) {
    LOGI(LOG_TAG_UI, "Restore vol after temp mute (prev=%u)", (unsigned)g_tempMutePrevVolIdx);
    g_currentVolIdx  = g_tempMutePrevVolIdx;
    g_tempMuteActive = false;
    if (g_dfPowered && g_dfOk) dfSetVolumeSafe((uint8_t)VOL_LEVELS[g_currentVolIdx]);
    forceIdleRedraw = true;
  }
}

void checkTempMuteRestore() {
  if (!g_tempMuteActive) return;
  if (!g_dfIsPlaying || !g_dfPowered ||
      (g_tempMuteStartedMs != 0 &&
       (int32_t)(millis() - g_tempMuteStartedMs) > (int32_t)TEMP_MUTE_MAX_DURATION_MS)) {
    LOGI(LOG_TAG_UI, "Temp mute safety restore (playing=%d powered=%d)",
         g_dfIsPlaying ? 1 : 0, g_dfPowered ? 1 : 0);
    restoreVolumeAfterTempMute();
  }
}
