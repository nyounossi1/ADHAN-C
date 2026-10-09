/*
 * test_AD_47 — Factory-default prayer settings: Moonsighting / Hanafi /
 * Angle Based.
 *
 * Mirrors Globals.h DEFAULT_P_* constants, the kMethodNames/kSchoolNames/
 * kLatAdjNames tables, and loadSettings()'s NVS read + range validation.
 *
 * AC coverage:
 *   AC-1  Defaults resolve to Moonsighting (15), Hanafi (1), Angle Based (3).
 *   AC-2  A first-boot / factory-reset device (no NVS keys) gets the new
 *         defaults; existing user NVS values survive a firmware update.
 *         Out-of-range stored values fall back to the new defaults.
 *   AC-4  The Aladhan query carries method=15&school=1&latitudeAdjustmentMethod=3.
 */

#include <unity.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Mirrors Globals.h / Globals.cpp
// ============================================================================
static constexpr int DEFAULT_P_METHOD = 15;
static constexpr int DEFAULT_P_SCHOOL = 1;
static constexpr int DEFAULT_P_LATADJ = 3;

static const char* kMethodNames[24] = {
  "Jafari","Karachi","ISNA","MWL","Makkah","Egypt",
  "", "Tehran","Gulf","Kuwait","Qatar","Singapore",
  "France","Turkey","Russia","Moonsighting","Dubai",
  "Malaysia","Tunisia","Algeria","Indonesia","Morocco",
  "Lisbon","Jordan"
};
static const char* kSchoolNames[2] = { "Shafi", "Hanafi" };
static const char* kLatAdjNames[4] = {
  "None", "Middle of Night", "1/7 Night", "Angle Based"
};

// Fake NVS: a key is either present with a value or absent.
struct StoredInt { bool present; int value; };
static int getInt(const StoredInt& k, int def) { return k.present ? k.value : def; }

struct PrayerSettings { int method; int school; int latAdj; };

// Mirrors the prayer-setting part of loadSettings()
static PrayerSettings loadPrayerSettings(StoredInt m, StoredInt s, StoredInt l) {
  PrayerSettings p = { DEFAULT_P_METHOD, DEFAULT_P_SCHOOL, DEFAULT_P_LATADJ };
  p.method = getInt(m, p.method);
  p.school = getInt(s, p.school);
  p.latAdj = getInt(l, p.latAdj);
  if (p.method < 0 || p.method > 23) p.method = DEFAULT_P_METHOD;
  if (p.school < 0 || p.school > 1)  p.school = DEFAULT_P_SCHOOL;
  if (p.latAdj < 0 || p.latAdj > 3)  p.latAdj = DEFAULT_P_LATADJ;
  return p;
}

static const StoredInt ABSENT = { false, 0 };

// ============================================================================
// Tests
// ============================================================================
void test_defaults_resolve_to_expected_names() {
  TEST_ASSERT_EQUAL_STRING("Moonsighting", kMethodNames[DEFAULT_P_METHOD]);
  TEST_ASSERT_EQUAL_STRING("Hanafi",       kSchoolNames[DEFAULT_P_SCHOOL]);
  TEST_ASSERT_EQUAL_STRING("Angle Based",  kLatAdjNames[DEFAULT_P_LATADJ]);
}

void test_fresh_device_gets_new_defaults() {
  PrayerSettings p = loadPrayerSettings(ABSENT, ABSENT, ABSENT);
  TEST_ASSERT_EQUAL_INT(15, p.method);
  TEST_ASSERT_EQUAL_INT(1,  p.school);
  TEST_ASSERT_EQUAL_INT(3,  p.latAdj);
}

void test_existing_user_values_survive_update() {
  // User previously chose ISNA / Shafi / Middle of Night
  PrayerSettings p = loadPrayerSettings({ true, 2 }, { true, 0 }, { true, 1 });
  TEST_ASSERT_EQUAL_INT(2, p.method);
  TEST_ASSERT_EQUAL_INT(0, p.school);
  TEST_ASSERT_EQUAL_INT(1, p.latAdj);
}

void test_out_of_range_values_fall_back_to_new_defaults() {
  PrayerSettings p = loadPrayerSettings({ true, 99 }, { true, -1 }, { true, 7 });
  TEST_ASSERT_EQUAL_INT(DEFAULT_P_METHOD, p.method);
  TEST_ASSERT_EQUAL_INT(DEFAULT_P_SCHOOL, p.school);
  TEST_ASSERT_EQUAL_INT(DEFAULT_P_LATADJ, p.latAdj);
}

void test_aladhan_query_uses_defaults() {
  // Mirrors PrayerEngine.cpp's query parameter construction
  PrayerSettings p = loadPrayerSettings(ABSENT, ABSENT, ABSENT);
  char q[96];
  snprintf(q, sizeof(q), "&method=%d&school=%d&latitudeAdjustmentMethod=%d",
           p.method, p.school, p.latAdj);
  TEST_ASSERT_EQUAL_STRING("&method=15&school=1&latitudeAdjustmentMethod=3", q);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_resolve_to_expected_names);
  RUN_TEST(test_fresh_device_gets_new_defaults);
  RUN_TEST(test_existing_user_values_survive_update);
  RUN_TEST(test_out_of_range_values_fall_back_to_new_defaults);
  RUN_TEST(test_aladhan_query_uses_defaults);
  return UNITY_END();
}
