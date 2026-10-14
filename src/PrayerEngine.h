/*
 * PrayerEngine.h — Prayer time fetching, parsing, and next-prayer computation.
 */
#pragma once
#include "Globals.h"
#include "PrayerCalc.h"

bool fetchLocationFromWifi();
bool fetchPrayerForDate(const String& dateStr, PrayerTimesDay& out);
bool fetchPrayerTimesTwoDays();
bool syncTimeNtp(uint32_t timeoutMs = 20000);

void computeNextPrayer(PrayerId& outId, int& outMin, bool& outFromTomorrow);
bool getNextPrayerCountdown(char* out, size_t outLen);

// Smart calculation (AD-49): best-fit method/school/latitude rule from mosque times
bool   smartFitToday(int mosqueFajr, int mosqueAsr, PcFit& out);  // false if location/time unknown
String smartFitSummary(const PcFit& fit);
void   applySmartFit(const PcFit& fit, int mosqueFajr, int mosqueAsr);  // set + persist settings
void   runPendingSmartFit();   // call after location + NTP, before fetching prayers
void   confirmSmartFit();      // call after prayers were fetched with the new settings

// Qibla
bool computeQiblaBearingDeg(float& outDeg);
const char* bearingToCompass16(float deg);
