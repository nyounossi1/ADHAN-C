/*
 * UiTask.h — UI state machine, menu engine, display drawing, input task.
 */
#pragma once
#include "Globals.h"
#include "ArcIdleRenderer.h"

// FreeRTOS task entry points
void uiTask(void*);
void inputTask(void*);
void prayerChimeTask(void*);

// ISRs (must be in scope for attachInterrupt in main)
void IRAM_ATTR isrBtnUp();
void IRAM_ATTR isrBtnDn();
void IRAM_ATTR isrBtnOk();
void IRAM_ATTR isrDfBusy();

// Timer callbacks
void splashTimerCb(TimerHandle_t);
void tickTimerCb(TimerHandle_t);
void ledPulseTimerCb(TimerHandle_t);

// LED pulse (used by input handlers)
void pulseLeds(uint8_t mask, uint32_t ms = 1200);

// Arc idle helpers (used by UI task)
void arcIdleApplyPrefs();
void arcIdleUpdateFromToday();

// Power management
void enablePm();
