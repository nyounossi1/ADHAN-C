/*
 * test_AD_57 — prayer times must follow the calendar day, including DST changes.
 *
 * Compiles src/PrayerDay.h directly (plain C++) and runs it under real POSIX TZ rules
 * (setenv TZ + tzset), the same rule strings the firmware uses (IANA_TO_POSIX).
 *
 * AC coverage:
 *   AC-1  Prayer times are refetched on every new local day (a never-refreshing device is shown to fail)
 *   AC-2  At local midnight tomorrow's times become today's with no network
 *   AC-3  A stale cache is flagged for refetch (the Wi-Fi task retries instead of waiting a day)
 *   AC-4  The next calendar day is correct on 23 h and 25 h DST days
 *   AC-5  Six months across the October and March Europe/London changes: the displayed
 *         "today" always matches the times for the local date
 */

#include <unity.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../../src/PrayerDay.h"

static const char* TZ_LONDON = "GMT0BST,M3.5.0/1,M10.5.0/2";

static void useTz(const char* tz) { setenv("TZ", tz, 1); tzset(); }

static time_t at(int y, int mo, int d, int h, int mi) {
  struct tm t = {};
  t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
  t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1;
  return mktime(&t);
}

// ---- Fake Aladhan: minutes after local midnight, in the UTC offset of that date -------
struct Day { int16_t fajr = -1; };

static int offsetMinAtNoon(uint32_t key) {
  struct tm t = {};
  t.tm_year = (int)(key / 10000) - 1900; t.tm_mon = (int)(key / 100 % 100) - 1;
  t.tm_mday = (int)(key % 100); t.tm_hour = 12; t.tm_isdst = -1;
  time_t n = mktime(&t);
  struct tm lt;
  localtime_r(&n, &lt);
  return (int)(lt.tm_gmtoff / 60);
}
static Day oracle(uint32_t key) {
  Day d;
  d.fajr = (int16_t)(300 + (int)(key % 50) + offsetMinAtNoon(key));
  return d;
}

// ---- Device model (what g_today / g_tomorrow / keys do in firmware) -------------------
static Day today, tomorrow;
static uint32_t todayKey, tomorrowKey;
static bool refetchFlag;

static void fetchTwoDays(time_t now) {   // fetchPrayerTimesTwoDays()
  uint32_t kT = pdLocalKey(now), kN = pdLocalKey(pdNextDayNoon(now));
  today = oracle(kT); tomorrow = oracle(kN);
  todayKey = kT; tomorrowKey = kN;
  refetchFlag = false;
}
static bool tick(time_t now) {            // prayerRollIfNeeded()
  uint32_t before = todayKey;
  if (pdAdvance(today, tomorrow, todayKey, tomorrowKey, pdLocalKey(now), pdLocalKey(pdNextDayNoon(now))))
    refetchFlag = true;
  return todayKey != before;
}

void setUp() { useTz(TZ_LONDON); today = Day(); tomorrow = Day(); todayKey = tomorrowKey = 0; refetchFlag = false; }
void tearDown() {}

// ---- AC-4 ------------------------------------------------------------------------------
void test_next_day_on_normal_day() {
  TEST_ASSERT_EQUAL_UINT32(20260610, pdLocalKey(at(2026, 6, 10, 9, 0)));
  TEST_ASSERT_EQUAL_UINT32(20260611, pdLocalKey(pdNextDayNoon(at(2026, 6, 10, 9, 0))));
  TEST_ASSERT_EQUAL_UINT32(20270101, pdLocalKey(pdNextDayNoon(at(2026, 12, 31, 23, 59))));
  TEST_ASSERT_EQUAL_UINT32(20280229, pdLocalKey(pdNextDayNoon(at(2028, 2, 28, 10, 0))));
}

void test_next_day_on_25h_autumn_day() {
  // 25 Oct 2026 is 25 hours long. now+86400 from 00:30 lands on the same date.
  time_t t = at(2026, 10, 25, 0, 30);
  TEST_ASSERT_EQUAL_UINT32(20261025, pdLocalKey(t + 86400));   // the old computation: wrong
  TEST_ASSERT_EQUAL_UINT32(20261026, pdLocalKey(pdNextDayNoon(t)));
  TEST_ASSERT_EQUAL_UINT32(20261026, pdLocalKey(pdNextDayNoon(at(2026, 10, 25, 23, 30))));
}

void test_next_day_on_23h_spring_day() {
  // 29 Mar 2026 is 23 hours long, so now+86400 from 23:30 on 28 Mar skips 29 Mar entirely.
  time_t t = at(2026, 3, 28, 23, 30);
  TEST_ASSERT_EQUAL_UINT32(20260330, pdLocalKey(t + 86400));   // the old computation: wrong
  TEST_ASSERT_EQUAL_UINT32(20260329, pdLocalKey(pdNextDayNoon(t)));
}

static uint32_t civilNext(uint32_t key) {
  int y = (int)(key / 10000), m = (int)(key / 100 % 100), d = (int)(key % 100);
  static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int len = dim[m - 1] + ((m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) ? 1 : 0);
  if (++d > len) { d = 1; if (++m > 12) { m = 1; y++; } }
  return pdKey(y, m, d);
}

void test_next_day_correct_every_hour_for_two_years_in_several_zones() {
  const char* zones[] = {
    "GMT0BST,M3.5.0/1,M10.5.0/2",          // London
    "CET-1CEST,M3.5.0,M10.5.0/3",          // Paris
    "EST5EDT,M3.2.0,M11.1.0",              // New York
    "AEST-10AEDT,M10.1.0,M4.1.0/3",        // Sydney (southern hemisphere)
    "<+03>-3",                             // Riyadh, no DST
  };
  for (const char* z : zones) {
    useTz(z);
    time_t t = at(2026, 1, 1, 0, 0);
    const time_t end = t + (time_t)730 * 86400;
    for (; t < end; t += 3600) {
      uint32_t k = pdLocalKey(t);
      if (pdLocalKey(pdNextDayNoon(t)) != civilNext(k)) {
        char msg[96];
        snprintf(msg, sizeof(msg), "zone %s key %u", z, (unsigned)k);
        TEST_FAIL_MESSAGE(msg);
      }
    }
  }
}

// ---- AC-1 / AC-2 -----------------------------------------------------------------------
void test_keep_on_same_day() {
  fetchTwoDays(at(2026, 10, 24, 12, 0));
  Day before = today;
  TEST_ASSERT_FALSE(tick(at(2026, 10, 24, 23, 59)));
  TEST_ASSERT_FALSE(refetchFlag);
  TEST_ASSERT_EQUAL_INT16(before.fajr, today.fajr);
}

void test_midnight_promotes_tomorrow_even_across_the_october_dst_change() {
  fetchTwoDays(at(2026, 10, 24, 12, 0));          // BST day, tomorrow (25 Oct) fetched in GMT terms
  int16_t bstFajr = today.fajr, gmtFajr = tomorrow.fajr;
  TEST_ASSERT_EQUAL_INT16(bstFajr, oracle(20261024).fajr);
  TEST_ASSERT_EQUAL_INT16(gmtFajr, oracle(20261025).fajr);
  TEST_ASSERT_EQUAL_INT(60, offsetMinAtNoon(20261024));   // BST
  TEST_ASSERT_EQUAL_INT(0,  offsetMinAtNoon(20261025));   // GMT

  TEST_ASSERT_FALSE(tick(at(2026, 10, 24, 23, 59)));
  TEST_ASSERT_TRUE(tick(at(2026, 10, 25, 0, 0)));
  TEST_ASSERT_EQUAL_UINT32(20261025, todayKey);
  TEST_ASSERT_EQUAL_INT16(gmtFajr, today.fajr);   // offline: correct GMT times, not the BST ones
  TEST_ASSERT_TRUE(refetchFlag);                  // the Wi-Fi task must still fetch the real tomorrow
  TEST_ASSERT_EQUAL_UINT32(20261026, tomorrowKey);
}

void test_exactly_one_roll_per_local_midnight_through_the_repeated_hour() {
  fetchTwoDays(at(2026, 10, 24, 12, 0));
  int rolls = 0;
  const time_t end = at(2026, 10, 26, 12, 0);
  for (time_t t = at(2026, 10, 24, 12, 0); t < end; t += 60) {
    if (tick(t)) {
      rolls++;
      refetchFlag = false;       // pretend the Wi-Fi task keeps up
      fetchTwoDays(t);
    }
  }
  TEST_ASSERT_EQUAL_INT(2, rolls);   // 25 Oct 00:00 and 26 Oct 00:00, none at the 01:00-02:00 repeat
}

void test_the_old_behaviour_is_wrong_after_the_clock_change() {
  fetchTwoDays(at(2026, 10, 22, 12, 0));      // last boot, three days before the change
  Day boot = today;
  time_t now = at(2026, 10, 25, 9, 0);        // no roll, no refetch: the reported bug
  TEST_ASSERT_NOT_EQUAL(oracle(pdLocalKey(now)).fajr, boot.fajr);
}

// ---- AC-3 ------------------------------------------------------------------------------
void test_offline_for_days_keeps_showing_data_and_flags_a_refetch() {
  fetchTwoDays(at(2026, 10, 20, 12, 0));
  Day shown = today;
  TEST_ASSERT_EQUAL(PD_STALE, pdPlan(todayKey, tomorrowKey, pdLocalKey(at(2026, 10, 25, 9, 0))));
  TEST_ASSERT_FALSE(tick(at(2026, 10, 25, 9, 0)));   // nothing promoted
  TEST_ASSERT_TRUE(refetchFlag);
  TEST_ASSERT_EQUAL_INT16(shown.fajr, today.fajr);   // better old times than silence
}

void test_unknown_keys_are_stale() {
  TEST_ASSERT_EQUAL(PD_STALE, pdPlan(0, 0, 20261025));
  TEST_ASSERT_EQUAL(PD_KEEP,  pdPlan(20261025, 20261026, 20261025));
  TEST_ASSERT_EQUAL(PD_ROLL,  pdPlan(20261024, 20261025, 20261025));
}

// ---- AC-5 ------------------------------------------------------------------------------
// Device is switched on once, then the Wi-Fi task refetches at 01:00 each new local day,
// exactly like wifiTask. Every minute for six months the shown "today" must equal the
// times for the local date.
static void run_six_months(int sy, int sm, int sd, int ey, int em, int ed, bool wifiUp) {
  time_t t = at(sy, sm, sd, 12, 0);
  const time_t end = at(ey, em, ed, 12, 0);
  fetchTwoDays(t);
  uint32_t lastDaily = pdLocalKey(t);
  int mismatches = 0, refetches = 0;
  for (; t < end; t += 60) {
    tick(t);
    struct tm lt; localtime_r(&t, &lt);
    uint32_t k = pdLocalKey(t);
    if (wifiUp && k != lastDaily && lt.tm_hour >= 1) {   // wifiTask daily refresh
      fetchTwoDays(t); lastDaily = k; refetches++;
    }
    if (today.fajr != oracle(k).fajr) mismatches++;
  }
  TEST_ASSERT_EQUAL_INT(0, mismatches);
  if (wifiUp) TEST_ASSERT_GREATER_THAN(150, refetches);
}

void test_six_months_across_october_change_wifi_up() { run_six_months(2026, 10, 1, 2027, 4, 5, true); }

void test_six_months_across_march_change_wifi_up()   { run_six_months(2026, 3, 1, 2026, 9, 1, true); }

void test_two_days_across_each_change_with_wifi_down_never_shows_wrong_offset() {
  // Radio off for the whole window: the midnight promotion alone carries the device over
  // the change (the second offline midnight would only show an estimate, so stop before it).
  run_six_months(2026, 10, 24, 2026, 10, 25, false);
  run_six_months(2026, 3, 28, 2026, 3, 29, false);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_next_day_on_normal_day);
  RUN_TEST(test_next_day_on_25h_autumn_day);
  RUN_TEST(test_next_day_on_23h_spring_day);
  RUN_TEST(test_next_day_correct_every_hour_for_two_years_in_several_zones);
  RUN_TEST(test_keep_on_same_day);
  RUN_TEST(test_midnight_promotes_tomorrow_even_across_the_october_dst_change);
  RUN_TEST(test_exactly_one_roll_per_local_midnight_through_the_repeated_hour);
  RUN_TEST(test_the_old_behaviour_is_wrong_after_the_clock_change);
  RUN_TEST(test_offline_for_days_keeps_showing_data_and_flags_a_refetch);
  RUN_TEST(test_unknown_keys_are_stale);
  RUN_TEST(test_six_months_across_october_change_wifi_up);
  RUN_TEST(test_six_months_across_march_change_wifi_up);
  RUN_TEST(test_two_days_across_each_change_with_wifi_down_never_shows_wrong_offset);
  return UNITY_END();
}
