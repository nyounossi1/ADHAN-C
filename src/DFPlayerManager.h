/*
 * DFPlayerManager.h — DFPlayer power control, UART, volume, playback.
 */
#pragma once
#include "Globals.h"

void dfSetVolumeSafe(uint8_t v);
void dfPlayFolderSafe(uint8_t folder, uint8_t track);
bool initDFPlayer();
void dfUartSafeOff();
void dfPowerOn();
void dfPowerOff();
void dfScheduleOff(uint32_t ms);

// Volume helpers (UI-facing)
void volumeInc();
void volumeDec();
void instantMute();
void restoreVolumeAfterTempMute();
void checkTempMuteRestore();

// Timer + ISR (called from main setup)
extern TimerHandle_t dfOffTimer;
extern SemaphoreHandle_t g_dfOffMtx;
void dfOffTimerCb(TimerHandle_t);
