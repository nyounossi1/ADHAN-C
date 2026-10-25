/*
 * PrayerDay.h — which calendar day the cached prayer times belong to (AD-57).
 *
 * Header-only and plain C++ so test_AD_57 can simulate DST changes natively.
 *   - local-date keys (YYYYMMDD) and the next local day, correct on 23 h / 25 h days
 *   - midnight rollover: tomorrow's times become today's without a network fetch
 *
 * The cached times are minutes after local midnight in the UTC offset of their own day
 * (the fetch passes the date in the path), so rolling tomorrow into today keeps them
 * correct across a DST change.
 */
#pragma once
#include <stdint.h>
#include <time.h>

enum PdAction : uint8_t {
  PD_KEEP = 0,   // cached "today" is today
  PD_ROLL,       // cached "tomorrow" is today: promote it
  PD_STALE       // cached data is older than that (or unknown): keep showing it, refetch
};

inline uint32_t pdKey(int year, int month, int day) {
  return (uint32_t)year * 10000u + (uint32_t)month * 100u + (uint32_t)day;
}

inline uint32_t pdLocalKey(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);
  return pdKey(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

// Local noon of the next calendar day. "now + 86400" is wrong on DST days: a 25 h day
// makes it land on the same date, a 23 h day makes it skip one. Noon is far from the
// 01:00-03:00 transition, so mktime() normalisation cannot pick the wrong side.
inline time_t pdNextDayNoon(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);
  lt.tm_mday += 1;
  lt.tm_hour = 12; lt.tm_min = 0; lt.tm_sec = 0;
  lt.tm_isdst = -1;
  return mktime(&lt);
}

inline PdAction pdPlan(uint32_t todayKey, uint32_t tomorrowKey, uint32_t nowKey) {
  if (todayKey != 0 && nowKey == todayKey) return PD_KEEP;
  if (tomorrowKey != 0 && nowKey == tomorrowKey) return PD_ROLL;
  return PD_STALE;
}

// Apply pdPlan(). nextKey is the local date after nowKey. Returns true when a refetch is
// needed (always after a roll: the new "tomorrow" is only an estimate copied from today,
// so computeNextPrayer() keeps working after Isha until the real times arrive).
template <class Day>
inline bool pdAdvance(Day& today, Day& tomorrow, uint32_t& todayKey, uint32_t& tomorrowKey,
                      uint32_t nowKey, uint32_t nextKey) {
  switch (pdPlan(todayKey, tomorrowKey, nowKey)) {
    case PD_KEEP:  return false;
    case PD_ROLL:
      today = tomorrow;
      todayKey = tomorrowKey;
      tomorrowKey = nextKey;   // estimate: same times as today, replaced by the next fetch
      return true;
    default:       return true;
  }
}
