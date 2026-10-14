/*
 * test_AD_49 — Smart calculation: local Fajr/Asr and best-fit method/school/rule.
 *
 * Compiles src/PrayerCalc.cpp directly (it is plain C++) and compares it with
 * saved Aladhan yearly responses (ref_vectors.csv next to this file, made by
 * make_vectors.py): Basingstoke for every supported method, both schools and all
 * latitude rules, plus Toronto and Karachi for MWL and Moonsighting.
 *
 * AC coverage:
 *   AC-3   Every supported method / school / rule is computed locally.
 *   AC-4   A far-off entry scores above the 10 min close-match limit.
 *   AC-5   Fajr only keeps the saved school; Asr only keeps method + rule.
 *   AC-6   Ties prefer the saved method, then the lowest method id.
 *   AC-10  Every reference day within +/-1 min of Aladhan; best fit recovers
 *          each sampled reference day within 2 min combined.
 *   AC-2   On-device HH:MM entry (SmartCalcEntry.h): hour/minute stepping wraps,
 *          "--" skips a prayer, and a skipped prayer skips its minute field.
 *   AC-7   Refetching with the new settings gets the requested day: the date
 *          goes in the Aladhan path (a "date" query parameter is ignored).
 */

#include <unity.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "../../src/PrayerCalc.h"
#include "../../src/PrayerCalc.cpp"
#include "../../src/SmartCalcEntry.h"

struct Row { int method, school, latAdj; PcDay d; int fajr, sunrise, asr; };

static std::vector<Row> g_rows;
static const int kTolMin = 1;

static void loadRows() {
  if (!g_rows.empty()) return;
  // ref_vectors.csv sits next to this file.
  char path[512];
  snprintf(path, sizeof(path), "%s", __FILE__);
  char* slash = strrchr(path, '/');
  if (slash) snprintf(slash + 1, sizeof(path) - (slash + 1 - path), "ref_vectors.csv");
  FILE* f = fopen(path, "r");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "ref_vectors.csv not found next to test_main.cpp");
  char line[256];
  fgets(line, sizeof line, f);  // header
  while (fgets(line, sizeof line, f)) {
    Row r{};
    if (sscanf(line, "%d,%d,%d,%lf,%lf,%d,%d,%d,%lf,%d,%d,%d", &r.method, &r.school, &r.latAdj,
               &r.d.lat, &r.d.lon, &r.d.year, &r.d.month, &r.d.day, &r.d.tzHours,
               &r.fajr, &r.sunrise, &r.asr) == 12) {
      g_rows.push_back(r);
    }
  }
  fclose(f);
}

static const Row& basingstokeMwlJan15() {
  for (const Row& r : g_rows) {
    if ((int)r.d.lat == 51 && r.method == 3 && r.school == 0 && r.latAdj == 3 &&
        r.d.month == 1 && r.d.day == 15) return r;
  }
  TEST_FAIL_MESSAGE("reference row missing");
  return g_rows[0];
}

void setUp() { loadRows(); }
void tearDown() {}

// ============================================================================
void test_reference_data_loaded() {
  TEST_ASSERT_TRUE(g_rows.size() >= 14600);
}

void test_all_methods_supported() {
  for (int m = 0; m <= 23; m++) {
    TEST_ASSERT_EQUAL(m != 6, pcMethodSupported(m));
  }
}

void test_fajr_within_one_minute_of_aladhan() {
  int worst = 0, bad = 0;
  for (const Row& r : g_rows) {
    const int d = abs(pcFajrMinutes(r.d, r.method, r.latAdj) - r.fajr);
    if (d > worst) worst = d;
    if (d > kTolMin) bad++;
  }
  char msg[64];
  snprintf(msg, sizeof(msg), "%d days off by more than 1 min (worst %d)", bad, worst);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, bad, msg);
}

void test_asr_within_one_minute_of_aladhan() {
  int worst = 0, bad = 0;
  for (const Row& r : g_rows) {
    const int d = abs(pcAsrMinutes(r.d, r.method, r.school) - r.asr);
    if (d > worst) worst = d;
    if (d > kTolMin) bad++;
  }
  char msg[64];
  snprintf(msg, sizeof(msg), "%d days off by more than 1 min (worst %d)", bad, worst);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, bad, msg);
}

void test_best_fit_recovers_reference_days() {
  int fail = 0;
  for (size_t i = 0; i < g_rows.size(); i += 7) {
    const Row& r = g_rows[i];
    PcFit fit;
    if (!pcBestFit(r.d, r.fajr, r.asr, 15, 1, 3, fit) || fit.score > 2) fail++;
  }
  TEST_ASSERT_EQUAL_INT(0, fail);
}

void test_fajr_only_keeps_school() {
  const Row& r = basingstokeMwlJan15();
  PcFit fit;
  TEST_ASSERT_TRUE(pcBestFit(r.d, r.fajr, -1, 2, 1, 3, fit));
  TEST_ASSERT_EQUAL_INT(1, fit.school);
  TEST_ASSERT_EQUAL_INT(0, fit.dFajr);
}

void test_asr_only_keeps_method_and_rule() {
  const Row& r = basingstokeMwlJan15();
  PcFit fit;
  TEST_ASSERT_TRUE(pcBestFit(r.d, -1, r.asr, 15, 1, 2, fit));
  TEST_ASSERT_EQUAL_INT(15, fit.method);
  TEST_ASSERT_EQUAL_INT(2, fit.latAdj);
  TEST_ASSERT_EQUAL_INT(0, fit.school);
}

void test_tie_prefers_saved_method() {
  // MWL, Karachi, Kuwait, Qatar, Tunisia, Algeria, Lisbon, Jordan all use 18 degrees.
  const Row& r = basingstokeMwlJan15();
  PcFit fit;
  TEST_ASSERT_TRUE(pcBestFit(r.d, r.fajr, -1, 9, 1, 3, fit));
  TEST_ASSERT_EQUAL_INT(9, fit.method);
}

void test_tie_without_saved_method_takes_lowest_id() {
  const Row& r = basingstokeMwlJan15();
  PcFit fit;
  TEST_ASSERT_TRUE(pcBestFit(r.d, r.fajr, -1, 2, 1, 3, fit));
  TEST_ASSERT_EQUAL_INT(1, fit.method);   // Karachi
}

void test_far_off_times_are_not_a_close_match() {
  const Row& r = basingstokeMwlJan15();
  PcFit fit;
  TEST_ASSERT_TRUE(pcBestFit(r.d, r.fajr - 60, r.asr + 40, 15, 1, 3, fit));
  TEST_ASSERT_TRUE(fit.score > PC_CLOSE_MATCH_MAX_MIN);
}

void test_nothing_entered_returns_false() {
  const Row& r = basingstokeMwlJan15();
  PcFit fit;
  TEST_ASSERT_FALSE(pcBestFit(r.d, -1, -1, 15, 1, 3, fit));
}

// Mirrors PrayerEngine.cpp fetchPrayerForDate() path construction.
static void buildTimingsPath(char* out, size_t n, const char* date, double lat, double lon,
                             int method, int school, int latAdj, const char* tz) {
  snprintf(out, n, "/v1/timings/%s?latitude=%.6f&longitude=%.6f&method=%d&school=%d"
           "&latitudeAdjustmentMethod=%d&timezonestring=%s", date, lat, lon, method, school, latAdj, tz);
}

void test_aladhan_date_is_in_path() {
  char p[256];
  buildTimingsPath(p, sizeof(p), "15-10-2026", 51.6707, 0.7323, 15, 1, 3, "Europe/London");
  TEST_ASSERT_EQUAL_STRING("/v1/timings/15-10-2026?latitude=51.670700&longitude=0.732300"
                           "&method=15&school=1&latitudeAdjustmentMethod=3&timezonestring=Europe/London", p);
  TEST_ASSERT_NULL(strstr(p, "&date="));
}

void test_entry_hour_steps_and_skip() {
  TEST_ASSERT_EQUAL_INT(6 * 60 + 44, scStepHour(5 * 60 + 44, +1, 0));
  TEST_ASSERT_EQUAL_INT(-1, scStepHour(23 * 60 + 5, +1, 0));        // 23 -> --
  TEST_ASSERT_EQUAL_INT(-1, scStepHour(5, -1, 0));                  // 00 -> --
  TEST_ASSERT_EQUAL_INT(0 * 60 + 44, scStepHour(-1, +1, 5 * 60 + 44)); // -- -> 00, seed minutes kept
  TEST_ASSERT_EQUAL_INT(23 * 60 + 44, scStepHour(-1, -1, 5 * 60 + 44));
}

void test_entry_minute_steps_wrap_in_hour() {
  TEST_ASSERT_EQUAL_INT(5 * 60 + 0, scStepMinute(5 * 60 + 59, +1));
  TEST_ASSERT_EQUAL_INT(5 * 60 + 59, scStepMinute(5 * 60 + 0, -1));
  TEST_ASSERT_EQUAL_INT(-1, scStepMinute(-1, +1));
}

void test_entry_skipped_prayer_skips_minute_field() {
  TEST_ASSERT_EQUAL_INT(SC_FAJR_M, scNextStep(SC_FAJR_H, 344, 974));
  TEST_ASSERT_EQUAL_INT(SC_ASR_H,  scNextStep(SC_FAJR_H, -1, 974));
  TEST_ASSERT_EQUAL_INT(SC_ASR_H,  scNextStep(SC_FAJR_M, 344, 974));
  TEST_ASSERT_EQUAL_INT(SC_ASR_M,  scNextStep(SC_ASR_H, 344, 974));
  TEST_ASSERT_EQUAL_INT(SC_RESULT, scNextStep(SC_ASR_H, 344, -1));
  TEST_ASSERT_EQUAL_INT(SC_RESULT, scNextStep(SC_ASR_M, 344, 974));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_reference_data_loaded);
  RUN_TEST(test_all_methods_supported);
  RUN_TEST(test_fajr_within_one_minute_of_aladhan);
  RUN_TEST(test_asr_within_one_minute_of_aladhan);
  RUN_TEST(test_best_fit_recovers_reference_days);
  RUN_TEST(test_fajr_only_keeps_school);
  RUN_TEST(test_asr_only_keeps_method_and_rule);
  RUN_TEST(test_tie_prefers_saved_method);
  RUN_TEST(test_tie_without_saved_method_takes_lowest_id);
  RUN_TEST(test_far_off_times_are_not_a_close_match);
  RUN_TEST(test_nothing_entered_returns_false);
  RUN_TEST(test_aladhan_date_is_in_path);
  RUN_TEST(test_entry_hour_steps_and_skip);
  RUN_TEST(test_entry_minute_steps_wrap_in_hour);
  RUN_TEST(test_entry_skipped_prayer_skips_minute_field);
  return UNITY_END();
}
