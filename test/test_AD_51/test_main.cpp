/*
 * test_AD_51 — Prayer Calculation: Auto / Manual modes and Asr Time.
 *
 * Compiles src/CalcMode.h and src/QrHelp.h directly (plain C++).
 *
 * AC coverage:
 *   AC-2  Asr Time labels: Mithl 1 (Hanafi) = school 1, Mithl 2 = school 0.
 *   AC-4  Auto sends no method / latitudeAdjustmentMethod; school is always sent.
 *   AC-5  Manual sends method, school and latitude rule as chosen.
 *   AC-7  calcMode defaults: stored mode wins; existing settings migrate to Manual;
 *         new / reset devices start in Auto.
 *   AC-9  The Help QR bitmap is 25x25 and has the three finder patterns.
 */

#include <unity.h>
#include <string.h>
#include "../../src/CalcMode.h"
#include "../../src/QrHelp.h"

void setUp() {}
void tearDown() {}

void test_asr_labels() {
  TEST_ASSERT_EQUAL_STRING("Mithl 1 (Hanafi)", cmAsrLabelDevice(1));
  TEST_ASSERT_EQUAL_STRING("Mithl 2 (Others)", cmAsrLabelDevice(0));
  TEST_ASSERT_EQUAL_STRING("Mithl 1: Hanafi", cmAsrLabelPortal(1));
  TEST_ASSERT_EQUAL_STRING("Mithl 2: Shafi'i, Maliki & Hanbali", cmAsrLabelPortal(0));
  TEST_ASSERT_TRUE(strlen(cmAsrLabelDevice(1)) <= 21 && strlen(cmAsrLabelDevice(0)) <= 21);  // OLED width
}

void test_auto_omits_method_and_rule() {
  char b[80];
  cmTimingsParams(b, sizeof(b), CALC_AUTO, 15, 1, 3);
  TEST_ASSERT_EQUAL_STRING("&school=1", b);
  TEST_ASSERT_NULL(strstr(b, "method"));
  TEST_ASSERT_NULL(strstr(b, "latitudeAdjustmentMethod"));
  cmTimingsParams(b, sizeof(b), CALC_AUTO, 3, 0, 1);
  TEST_ASSERT_EQUAL_STRING("&school=0", b);
}

void test_manual_sends_all_three() {
  char b[80];
  cmTimingsParams(b, sizeof(b), CALC_MANUAL, 15, 1, 3);
  TEST_ASSERT_EQUAL_STRING("&method=15&school=1&latitudeAdjustmentMethod=3", b);
  cmTimingsParams(b, sizeof(b), CALC_MANUAL, 3, 0, 2);
  TEST_ASSERT_EQUAL_STRING("&method=3&school=0&latitudeAdjustmentMethod=2", b);
}

void test_params_fit_the_firmware_buffer() {
  char b[72];   // buffer size used in PrayerEngine.cpp
  cmTimingsParams(b, sizeof(b), CALC_MANUAL, 23, 1, 3);
  TEST_ASSERT_TRUE(strlen(b) < sizeof(b) - 1);
}

void test_mode_resolution_and_migration() {
  TEST_ASSERT_EQUAL_UINT8(CALC_MANUAL, cmResolveMode(true, CALC_MANUAL, false));
  TEST_ASSERT_EQUAL_UINT8(CALC_AUTO,   cmResolveMode(true, CALC_AUTO, true));     // stored mode wins
  TEST_ASSERT_EQUAL_UINT8(CALC_AUTO,   cmResolveMode(true, 7, true));             // junk -> Auto
  TEST_ASSERT_EQUAL_UINT8(CALC_MANUAL, cmResolveMode(false, 0, true));            // existing device keeps its times
  TEST_ASSERT_EQUAL_UINT8(CALC_AUTO,   cmResolveMode(false, 0, false));           // new / factory reset
}

static bool qrModule(int r, int c) { return (QR_HELP_ROWS[r] >> (QR_HELP_SIZE - 1 - c)) & 1U; }

void test_qr_size_and_finder_patterns() {
  TEST_ASSERT_EQUAL_INT(25, QR_HELP_SIZE);
  const int corners[3][2] = { {0, 0}, {0, QR_HELP_SIZE - 7}, {QR_HELP_SIZE - 7, 0} };
  for (auto& k : corners) {
    for (int i = 0; i < 7; i++) {
      TEST_ASSERT_TRUE(qrModule(k[0], k[1] + i));          // top edge of the finder
      TEST_ASSERT_TRUE(qrModule(k[0] + 6, k[1] + i));      // bottom edge
      TEST_ASSERT_TRUE(qrModule(k[0] + i, k[1]));          // left edge
      TEST_ASSERT_TRUE(qrModule(k[0] + i, k[1] + 6));      // right edge
    }
    TEST_ASSERT_TRUE(qrModule(k[0] + 3, k[1] + 3));        // centre dot
    TEST_ASSERT_FALSE(qrModule(k[0] + 1, k[1] + 1));       // ring gap
  }
  TEST_ASSERT_TRUE(2 * QR_HELP_SIZE <= 64);                // fits the OLED height at 2 px/module
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_asr_labels);
  RUN_TEST(test_auto_omits_method_and_rule);
  RUN_TEST(test_manual_sends_all_three);
  RUN_TEST(test_params_fit_the_firmware_buffer);
  RUN_TEST(test_mode_resolution_and_migration);
  RUN_TEST(test_qr_size_and_finder_patterns);
  return UNITY_END();
}
