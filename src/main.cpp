/*
 * File: ADHAI_0_3_08.ino 
 * Author: Najeeb Younossi
 * Project: A:DHAN AI Adhan Clock
 * Description: ESP32-based prayer time reminder with LED indicators, audio chimes, 
 *              and WiFi configuration portal. Uses FreeRTOS for multi-tasking.
 * 
 * Revision History:
 * Version | Date       | Author      | Description
 * --------|------------|-------------|----------------------------------------------
 *  0.3.08 | 2026-02-08 | Developer   | Hard reset on Ok button pressed > 10s
 *  0.3.07 | 2026-02-08 | Developer   | Fix bug with incorrect wifi credentials
 *  0.3.06 | 2026-02-08 | Developer   | Creating Modules
 *  0.3.05 | 2026-02-08 | Developer   | Code optimisation and clean up
 *  0.3.04 | 2026-02-08 | Developer   | Updated set up instructions
 *  0.3.03 | 2026-02-04 | Developer   | Next prayer count down timer on idle screen
 *  0.3.02 | 2026-02-04 | Developer   | Repositioned the Notification banner.
 *  0.3.01 | 2026-02-03 | Developer   | Fix instant murte issue
 *  0.3.00 | 2026-02-03 | Developer   | UI structure major overhaul
 *  0.2.09 | 2026-02-02 | Developer   | Show current prayer during Playback
 *  0.2.08 | 2026-01-30 | Developer   | Improve FOTA notification banner
 *         |            |             | Fix arc position by adding sunrise
 *         |            |             | Make Asr position flexible on the curve
 *         |            |             | Changed VOlume levels to 0,7,14,21
 *  0.2.07 | 2026-01-28 | Developer   | Changing Idle UI
 *  0.2.06 | 2026-01-27 | Developer   | Add prayer calculations settings
 *  0.2.05 | 2026-01-27 | Developer   | Menu Restructure
 *  0.2.04 | 2026-01-27 | Developer   | Remove Autobrightness and adding Qibla finder.
 *         |            |             | Fixed an issue where volume did not work during playback
 *  0.2.03 | 2026-01-26 | Developer   | Fine tune the display sleep behaviour
 *  0.2.02 | 2026-01-23 | Developer   | Switch off display to save power
 *  0.2.01 | 2026-01-23 | Developer   | Long press okay to Mute. Add adhan test to menu
 *  0.2.00 | 2026-01-23 | Developer   | More buttons, Switcxh for DF Player and LED lights
 *  0.1.03 | 2026-01-14 | Developer   | Fix return to idle delays.
 *  0.1.02 | 2026-01-10 | Developer   | TBC.
 *  0.1.01 | 2026-01-09 | Developer   | Remove DFPlayer BUSY feature.
 *  0.1.00 | 2026-01-08 | Developer   | Power optimisations, Brightness control
 */


#include <Wire.h>
#include "Globals.h"
#include "DFPlayerManager.h"
#include "PrayerEngine.h"
#include "FotaManager.h"
#include "WifiManager.h"
#include "UiTask.h"
#include <Arduino.h>

// ============================================================================
// Setup
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  // Power save
  enablePm();
  setCpuFrequencyMhz(60);

  // Logging mutex (first, so all other init can log)
  g_logMtx = xSemaphoreCreateMutex();
  if (!g_logMtx) while (true) delay(1000);

  LOGI(LOG_TAG_SYS, "FW boot start");
  LOGI(LOG_TAG_SYS, "Version %s", FW_VER);

  loadSettings();

  // GPIO
  pinMode(BTN_UP, INPUT);
  pinMode(BTN_OK, INPUT);
  pinMode(BTN_DN, INPUT_PULLDOWN);
  pinMode(OK_LED_PIN, OUTPUT);
  pinMode(BTN_LED_PIN, OUTPUT);
  ledsAllOff();

  // DFPlayer power switch (off at boot)
  pinMode(DF_ON_PIN, OUTPUT);
  digitalWrite(DF_ON_PIN, LOW);
  dfUartSafeOff();

  // DF off timer
  g_dfOffMtx = xSemaphoreCreateMutex();
  dfOffTimer = xTimerCreate("dfOff", pdMS_TO_TICKS(90000), pdFALSE, nullptr, dfOffTimerCb);

  // I²C + OLED
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);  // SH1106 supports 400 kHz

  if (!display.begin(OLED_ADDR, true)) {
    LOGE(LOG_TAG_OLED, "Display init failed");
    while (true) delay(1000);
  }
  LOGI(LOG_TAG_OLED, "SH1106 init OK @0x%02X", OLED_ADDR);
  display.setContrast(255);

  // RTOS synchronisation
  g_displayMtx = xSemaphoreCreateMutex();
  g_dataMtx    = xSemaphoreCreateMutex();
  g_fotaMtx    = xSemaphoreCreateMutex();
  g_splashMtx  = xSemaphoreCreateMutex();

  if (!g_displayMtx || !g_dataMtx) while (true) delay(1000);

  // Queues
  rawBtnQueue  = xQueueCreate(16, sizeof(RawBtnEvent));
  uiQueue      = xQueueCreate(64, sizeof(UiEvent));
  wifiCmdQueue = xQueueCreate(8,  sizeof(WifiCmd));
  if (!rawBtnQueue || !uiQueue || !wifiCmdQueue) while (true) delay(1000);

  // Timers
  splashTimer   = xTimerCreate("splash",   pdMS_TO_TICKS(5000), pdFALSE, nullptr, splashTimerCb);
  tickTimer     = xTimerCreate("tick",     pdMS_TO_TICKS(1000), pdTRUE,  nullptr, tickTimerCb);
  ledPulseTimer = xTimerCreate("ledPulse", pdMS_TO_TICKS(900),  pdFALSE, nullptr, ledPulseTimerCb);
  if (!splashTimer || !tickTimer || !ledPulseTimer) while (true) delay(1000);

  // Interrupts
  attachInterrupt(digitalPinToInterrupt(BTN_UP), isrBtnUp, RISING);
  attachInterrupt(digitalPinToInterrupt(BTN_DN), isrBtnDn, RISING);
  attachInterrupt(digitalPinToInterrupt(BTN_OK), isrBtnOk, RISING);
  attachInterrupt(digitalPinToInterrupt(DF_BUSY_PIN), isrDfBusy, CHANGE);

  updateSplashStatus("Powering Up...");

  // DF BUSY input
  pinMode(DF_BUSY_PIN, INPUT_PULLUP);

  // Tasks
  xTaskCreatePinnedToCore(uiTask,           "ui",    4096, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(inputTask,        "input", 2048, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(wifiTask,         "wifi",  8192, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(prayerChimeTask,  "adhan", 4096, nullptr, 1, nullptr, 1);

  LOGI(LOG_TAG_SYS, "FW boot complete");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
