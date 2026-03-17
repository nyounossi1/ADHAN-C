/*
 * PrayerEngine.h — Prayer time fetching, parsing, and next-prayer computation.
 */
#pragma once
#include "Globals.h"

bool fetchLocationFromWifi();
bool fetchPrayerForDate(const String& dateStr, PrayerTimesDay& out);
bool fetchPrayerTimesTwoDays();
bool syncTimeNtp(uint32_t timeoutMs = 20000);

void computeNextPrayer(PrayerId& outId, int& outMin, bool& outFromTomorrow);
bool getNextPrayerCountdown(char* out, size_t outLen);

// Qibla
bool computeQiblaBearingDeg(float& outDeg);
const char* bearingToCompass16(float deg);
