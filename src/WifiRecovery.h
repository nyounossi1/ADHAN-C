/*
 * WifiRecovery.h — decisions for getting back online by itself (AD-58, AD-59).
 *
 * Header-only and plain C++ so test_AD_58 can check it natively.
 *   - backoff schedule for the background retry (router still booting after a power cut)
 *   - what still has to be recovered (time, time zone, location, prayer times)
 *   - when the setup portal may open (never because a connect failed while credentials exist)
 *   - which location source to use (ip-api, fallback provider, cached fix) and when to save it
 */
#pragma once
#include <math.h>
#include <stddef.h>
#include <stdint.h>

// ---- Retry backoff ---------------------------------------------------------------------
static const uint32_t WR_BACKOFF_MS[] = { 30000, 60000, 120000, 240000, 300000 };
static constexpr uint32_t WR_BACKOFF_STEPS = (uint32_t)(sizeof(WR_BACKOFF_MS) / sizeof(WR_BACKOFF_MS[0]));

// Delay before attempt number `attempts` (0 = first retry); stays at 5 minutes forever.
inline uint32_t wrBackoffMs(uint32_t attempts) {
  return WR_BACKOFF_MS[attempts < WR_BACKOFF_STEPS ? attempts : WR_BACKOFF_STEPS - 1];
}

// millis() wrap-safe (unsigned subtraction).
inline bool wrDue(uint32_t nowMs, uint32_t lastMs, uint32_t attempts) {
  return (uint32_t)(nowMs - lastMs) >= wrBackoffMs(attempts);
}

// A full online session is still missing something the clock needs.
inline bool wrNeedsRecovery(bool timeSynced, bool tzConfigured, bool locationReady, bool prayerReady) {
  return !(timeSynced && tzConfigured && locationReady && prayerReady);
}

// A failed connect never erases credentials. The setup portal opens only when there are
// none, or when the user asked for it (menu / onboarding flow).
inline bool wrOpenPortal(bool haveCreds, bool userInitiated) {
  return !haveCreds || userInitiated;
}

// ---- Location source -------------------------------------------------------------------
enum LocSource : uint8_t { LS_NONE = 0, LS_IPAPI, LS_IPWHO, LS_CACHE };

inline LocSource lsChoose(bool ipApiOk, bool ipWhoOk, bool cacheValid) {
  if (ipApiOk) return LS_IPAPI;
  if (ipWhoOk) return LS_IPWHO;
  if (cacheValid) return LS_CACHE;
  return LS_NONE;
}

inline bool lsCacheValid(double lat, double lon, const char* tz) {
  if (!tz || !tz[0]) return false;
  if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return false;
  return !(lat == 0.0 && lon == 0.0);   // 0,0 is what an unset value looks like
}

// Write the cache only when it adds something: NVS flash wear matters.
inline bool lsCacheShouldSave(bool oldValid, double oldLat, double oldLon, const char* oldTz,
                              double lat, double lon, const char* tz) {
  if (!lsCacheValid(lat, lon, tz)) return false;
  if (!oldValid) return true;
  if (fabs(oldLat - lat) > 0.005 || fabs(oldLon - lon) > 0.005) return true;
  const char* a = oldTz ? oldTz : "";
  while (*a && *a == *tz) { a++; tz++; }
  return *a != *tz;
}
