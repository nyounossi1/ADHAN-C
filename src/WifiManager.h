/*
 * WifiManager.h — WiFi STA/AP, captive portal, session runners.
 */
#pragma once
#include "Globals.h"

void wifiRadioOff();
void wifiRadioOnSta();
bool connectStaOnce(const String& ssid, const String& pass, uint32_t timeoutMs = 12000);

// Session runners (connect -> do work -> radio off)
bool runWifiSessionRefresh(const String& ssid, const String& pass);
bool runWifiSessionFota(const String& ssid, const String& pass);
bool runWifiSessionFotaCheckOnly(const String& ssid, const String& pass, bool force);
bool connectStaWithRetry(const String& ssid, const String& pass);
// Portal
void startCaptivePortal();
void stopCaptivePortal();

// The WiFi FreeRTOS task entry point
void wifiTask(void*);

// Portal state (used by UI for onboarding page navigation)
extern int g_apOnboardPage;
extern const int AP_ONBOARD_PAGES;
extern const char* AP_SSID;
