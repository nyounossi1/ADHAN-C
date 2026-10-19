/*
 * test_AD_53 — FOTA: skip updates that do not fit the device's OTA slot.
 *
 * Compiles src/FotaFit.h directly (plain C++).
 *
 * AC coverage:
 *   AC-2  A slot large enough for the manifest size treats the update as available.
 *   AC-3  A slot smaller than the manifest size does not offer the update.
 *   AC-4  A manifest without a size field behaves as before (fits).
 *   AC-6  Fits, too big, exact fit and missing size are all covered.
 */

#include <unity.h>
#include "../../src/FotaFit.h"

static const size_t LEGACY_SLOT = 1310720;   // Arduino default partition table
static const size_t MIN_SPIFFS_SLOT = 1966080;

void setUp() {}
void tearDown() {}

void test_fits_in_both_slots_today() {
  TEST_ASSERT_TRUE(fotaFitsSlot(1276852, LEGACY_SLOT));
  TEST_ASSERT_TRUE(fotaFitsSlot(1276852, MIN_SPIFFS_SLOT));
}

void test_too_big_for_legacy_but_fits_new_slot() {
  TEST_ASSERT_FALSE(fotaFitsSlot(1400000, LEGACY_SLOT));
  TEST_ASSERT_TRUE(fotaFitsSlot(1400000, MIN_SPIFFS_SLOT));
}

void test_exact_fit_and_one_byte_over() {
  TEST_ASSERT_TRUE(fotaFitsSlot(1310720, LEGACY_SLOT));
  TEST_ASSERT_FALSE(fotaFitsSlot(1310721, LEGACY_SLOT));
}

void test_missing_or_invalid_size_behaves_as_before() {
  TEST_ASSERT_TRUE(fotaFitsSlot(0, LEGACY_SLOT));    // field absent -> 0
  TEST_ASSERT_TRUE(fotaFitsSlot(-1, LEGACY_SLOT));
}

void test_too_big_for_any_slot() {
  TEST_ASSERT_FALSE(fotaFitsSlot(2000000, MIN_SPIFFS_SLOT));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fits_in_both_slots_today);
  RUN_TEST(test_too_big_for_legacy_but_fits_new_slot);
  RUN_TEST(test_exact_fit_and_one_byte_over);
  RUN_TEST(test_missing_or_invalid_size_behaves_as_before);
  RUN_TEST(test_too_big_for_any_slot);
  return UNITY_END();
}
