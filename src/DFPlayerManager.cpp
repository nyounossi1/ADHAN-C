/*
 * DFPlayerManager.cpp — DFPlayer power, UART, volume, playback.
 */
#include "DFPlayerManager.h"
#include <DFRobotDFPlayerMini.h>

static DFRobotDFPlayerMini dfPlayer;
static SemaphoreHandle_t g_dfMtx = nullptr;

TimerHandle_t    dfOffTimer  = nullptr;
SemaphoreHandle_t g_dfOffMtx = nullptr;
static uint32_t  g_dfOffAtMs = 0;

// ============================================================================
// Mutex helpers
// ============================================================================
static inline bool dfTake(uint32_t ms) {
  if (!g_dfMtx) return true;
  return xSemaphoreTake(g_dfMtx, pdMS_TO_TICKS(ms)) == pdTRUE;
}

static inline void dfGive() {
  if (g_dfMtx) xSemaphoreGive(g_dfMtx);
}

// ============================================================================
// Safe playback / volume
// ============================================================================
void dfSetVolumeSafe(uint8_t v) {
  if (!g_dfPowered || !g_dfOk) return;
  if (!dfTake(60)) return;
  dfPlayer.volume(v);
  dfGive();
}

void dfPlayFolderSafe(uint8_t folder, uint8_t track) {
  if (!g_dfPowered || !g_dfOk) return;
  if (!dfTake(120)) return;
  dfPlayer.playFolder(folder, track);
  dfGive();
}

// ============================================================================
// Init
// ============================================================================
bool initDFPlayer() {
  if (!g_dfMtx) g_dfMtx = xSemaphoreCreateMutex();

  LOGI(LOG_TAG_SYS, "DF: Serial2 begin 9600 RX=%d TX=%d", DF_RX_PIN, DF_TX_PIN);
  Serial2.begin(9600, SERIAL_8N1, DF_RX_PIN, DF_TX_PIN);
  vTaskDelay(pdMS_TO_TICKS(300));

  g_dfOk = dfPlayer.begin(Serial2, false, true);

  if (!g_dfOk) {
    LOGE(LOG_TAG_SYS, "DF: begin() FAILED. Check wiring/power/SD card.");
    vTaskDelay(pdMS_TO_TICKS(200));
    g_dfOk = dfPlayer.begin(Serial2, true, false);
    LOGW(LOG_TAG_SYS, "DF: retry without reset -> %s", g_dfOk ? "OK" : "FAIL");
  } else {
    LOGI(LOG_TAG_SYS, "DF: begin() OK");
  }

  if (g_dfOk) {
    dfPlayer.setTimeOut(300);
    dfPlayer.outputDevice(DFPLAYER_DEVICE_SD);
    uint8_t vol = (uint8_t)VOL_LEVELS[g_currentVolIdx];
    dfPlayer.volume(vol);
    LOGI(LOG_TAG_SYS, "DF: volume set to %u (idx=%u)", (unsigned)vol, (unsigned)g_currentVolIdx);
  }

  return g_dfOk;
}

// ============================================================================
// UART safe-off (prevent phantom power via GPIO)
// ============================================================================
void dfUartSafeOff() {
  Serial2.end();
  pinMode(DF_TX_PIN, INPUT);
  pinMode(DF_RX_PIN, INPUT);
}

// ============================================================================
// Power on / off
// ============================================================================
void dfPowerOn() {
  if (g_dfPowered) return;
  dfUartSafeOff();
  digitalWrite(DF_ON_PIN, HIGH);
  g_dfPowered = true;
  vTaskDelay(pdMS_TO_TICKS(300));

  initDFPlayer();

  if (!g_dfOk) {
    dfUartSafeOff();
    digitalWrite(DF_ON_PIN, LOW);
    g_dfPowered = false;
  }
}

void dfPowerOff() {
  if (!g_dfPowered) return;
  dfUartSafeOff();
  digitalWrite(DF_ON_PIN, LOW);

  if (g_tempMuteActive) {
    restoreVolumeAfterTempMute();
  }

  g_dfPowered   = false;
  g_dfOk        = false;
  g_dfIsPlaying = false;
  g_dfBusyLevel = 1;

  LOGI(LOG_TAG_SYS, "DFPlayer powered off");
}

// ============================================================================
// Scheduled power-off timer
// ============================================================================
void dfOffTimerCb(TimerHandle_t) {
  uint32_t now   = millis();
  uint32_t offAt = 0;

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
  const uint32_t now   = millis();
  const uint32_t newAt = now + ms;

  if (g_dfOffMtx) xSemaphoreTake(g_dfOffMtx, portMAX_DELAY);
  if (g_dfOffAtMs == 0 || (int32_t)(newAt - g_dfOffAtMs) > 0) {
    g_dfOffAtMs = newAt;
  }
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
      g_currentVolIdx = 0;
      g_tempMuteActive = true;
      g_tempMuteStartedMs = millis();
      if (g_dfOk) dfSetVolumeSafe((uint8_t)VOL_LEVELS[g_currentVolIdx]);
      LOGI(LOG_TAG_UI, "Temporary MUTE during playback (prev=%u)", (unsigned)g_tempMutePrevVolIdx);
      forceIdleRedraw = true;
    }
  } else {
    if (g_currentVolIdx != 0) {
      g_prevVolIdx = g_currentVolIdx;
      g_currentVolIdx = 0;
      markSettingsDirty();
      if (g_dfPowered && g_dfOk) dfSetVolumeSafe((uint8_t)VOL_LEVELS[g_currentVolIdx]);
      LOGI(LOG_TAG_UI, "Regular MUTE (prev=%u)", (unsigned)g_prevVolIdx);
      forceIdleRedraw = true;
    }
  }
}

void restoreVolumeAfterTempMute() {
  if (g_tempMuteActive) {
    LOGI(LOG_TAG_UI, "Restoring volume after temp mute (prev=%u)", (unsigned)g_tempMutePrevVolIdx);
    g_currentVolIdx = g_tempMutePrevVolIdx;
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
    LOGI(LOG_TAG_UI, "Temp mute safety restore (playing=%d, powered=%d)",
         g_dfIsPlaying ? 1 : 0, g_dfPowered ? 1 : 0);
    restoreVolumeAfterTempMute();
  }
}
