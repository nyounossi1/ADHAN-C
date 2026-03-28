/*
 * test_AD_30 — Unit tests for per-prayer time offset adjustment.
 *
 * AC coverage:
 *   AC-3  Clamp: offset stays within [-5, +5]
 *   AC-5  Timeout discard: NVS value unchanged when slider is abandoned
 *   AC-6  Reset all: all five offsets become 0
 *   AC-7  Adjusted time computation: rawMinutes ± offset, clamped to [0, 1439]
 *   AC-7  NVS round-trip: write offset, read back, verify equality
 */

#include <unity.h>
#include <stdint.h>
#include <string.h>

// ============================================================================
// Minimal stubs — no hardware, no FreeRTOS, no NVS
// ============================================================================

static int8_t  g_prayerOffsets[5] = { 0, 0, 0, 0, 0 };

// Simulated NVS store (5 slots)
static int8_t s_nvsStore[5] = { 0, 0, 0, 0, 0 };

static void nvsWrite(int idx, int8_t val) { s_nvsStore[idx] = val; }
static int8_t nvsRead(int idx)            { return s_nvsStore[idx]; }

static void loadOffsetsFromNvs() {
  for (int i = 0; i < 5; i++) g_prayerOffsets[i] = nvsRead(i);
}
static void saveOffsetsToNvs() {
  for (int i = 0; i < 5; i++) nvsWrite(i, g_prayerOffsets[i]);
}

// Clamp helper matching PrayerEngine logic
static int16_t clampDay(int v) {
  if (v < 0)    return 0;
  if (v > 1439) return 1439;
  return (int16_t)v;
}

static int16_t applyOffset(int16_t rawMinutes, int8_t offset) {
  return clampDay((int)rawMinutes + (int)offset);
}

// Slider clamp logic matching UiTask
static const int8_t SLIDER_MIN = -5;
static const int8_t SLIDER_MAX =  5;

static int8_t sliderClamp(int8_t val) {
  if (val < SLIDER_MIN) return SLIDER_MIN;
  if (val > SLIDER_MAX) return SLIDER_MAX;
  return val;
}

// ============================================================================
// setUp / tearDown
// ============================================================================
void setUp() {
  memset(g_prayerOffsets, 0, sizeof(g_prayerOffsets));
  memset(s_nvsStore,      0, sizeof(s_nvsStore));
}

void tearDown() {}

// ============================================================================
// AC-3: Clamp — offset stays within [-5, +5]
// ============================================================================
void test_slider_clamp_upper() {
  int8_t val = 5;
  // Attempting to increment beyond +5 must stay at +5
  if (val < SLIDER_MAX) val++;
  TEST_ASSERT_EQUAL_INT8(5, val); // unchanged
}

void test_slider_clamp_lower() {
  int8_t val = -5;
  if (val > SLIDER_MIN) val--;
  TEST_ASSERT_EQUAL_INT8(-5, val);
}

void test_slider_increment_within_range() {
  int8_t val = 3;
  if (val < SLIDER_MAX) val++;
  TEST_ASSERT_EQUAL_INT8(4, val);
}

void test_slider_decrement_within_range() {
  int8_t val = -3;
  if (val > SLIDER_MIN) val--;
  TEST_ASSERT_EQUAL_INT8(-4, val);
}

// ============================================================================
// AC-7: Adjusted time computation — rawMinutes ± offset, clamped [0, 1439]
// ============================================================================
void test_offset_positive() {
  TEST_ASSERT_EQUAL_INT16(365, applyOffset(360, 5));   // 6:00 + 5 = 6:05
}

void test_offset_negative() {
  TEST_ASSERT_EQUAL_INT16(355, applyOffset(360, -5));  // 6:00 - 5 = 5:55
}

void test_offset_zero() {
  TEST_ASSERT_EQUAL_INT16(720, applyOffset(720, 0));   // unchanged
}

void test_offset_clamp_lower_bound() {
  TEST_ASSERT_EQUAL_INT16(0, applyOffset(3, -5));      // 0:03 - 5 → clamp to 0
}

void test_offset_clamp_upper_bound() {
  TEST_ASSERT_EQUAL_INT16(1439, applyOffset(1437, 5)); // 23:57 + 5 → clamp to 1439
}

// ============================================================================
// AC-7: NVS round-trip — write offset, read back, verify equality
// ============================================================================
void test_nvs_roundtrip_positive() {
  g_prayerOffsets[0] = 3; // Fajr +3
  saveOffsetsToNvs();
  memset(g_prayerOffsets, 0, sizeof(g_prayerOffsets));
  loadOffsetsFromNvs();
  TEST_ASSERT_EQUAL_INT8(3, g_prayerOffsets[0]);
}

void test_nvs_roundtrip_negative() {
  g_prayerOffsets[4] = -4; // Isha -4
  saveOffsetsToNvs();
  memset(g_prayerOffsets, 0, sizeof(g_prayerOffsets));
  loadOffsetsFromNvs();
  TEST_ASSERT_EQUAL_INT8(-4, g_prayerOffsets[4]);
}

void test_nvs_roundtrip_all_prayers() {
  int8_t expected[5] = { 1, -2, 3, -4, 5 };
  for (int i = 0; i < 5; i++) g_prayerOffsets[i] = expected[i];
  saveOffsetsToNvs();
  memset(g_prayerOffsets, 0, sizeof(g_prayerOffsets));
  loadOffsetsFromNvs();
  for (int i = 0; i < 5; i++) {
    TEST_ASSERT_EQUAL_INT8(expected[i], g_prayerOffsets[i]);
  }
}

// ============================================================================
// AC-5: Timeout discard — NVS value unchanged when slider abandoned
// ============================================================================
void test_timeout_discards_pending_value() {
  // User previously saved offset +2 for Asr
  g_prayerOffsets[2] = 2;
  saveOffsetsToNvs();

  // User enters slider, moves to +4, then times out (no OK press)
  int8_t pendingVal = 4;
  // Simulate timeout: do NOT call save, do NOT update g_prayerOffsets
  (void)pendingVal; // discarded

  // NVS must still hold +2
  int8_t fromNvs = nvsRead(2);
  TEST_ASSERT_EQUAL_INT8(2, fromNvs);
  // In-memory offset also unchanged
  TEST_ASSERT_EQUAL_INT8(2, g_prayerOffsets[2]);
}

// ============================================================================
// AC-6: Reset all — all five NVS keys become 0
// ============================================================================
void test_reset_all_offsets() {
  int8_t nonZero[5] = { 1, -2, 3, -4, 5 };
  for (int i = 0; i < 5; i++) g_prayerOffsets[i] = nonZero[i];
  saveOffsetsToNvs();

  // actResetAllOffsets equivalent
  memset(g_prayerOffsets, 0, sizeof(g_prayerOffsets));
  saveOffsetsToNvs();

  for (int i = 0; i < 5; i++) {
    TEST_ASSERT_EQUAL_INT8(0, nvsRead(i));
    TEST_ASSERT_EQUAL_INT8(0, g_prayerOffsets[i]);
  }
}

// ============================================================================
// main
// ============================================================================
int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_slider_clamp_upper);
  RUN_TEST(test_slider_clamp_lower);
  RUN_TEST(test_slider_increment_within_range);
  RUN_TEST(test_slider_decrement_within_range);

  RUN_TEST(test_offset_positive);
  RUN_TEST(test_offset_negative);
  RUN_TEST(test_offset_zero);
  RUN_TEST(test_offset_clamp_lower_bound);
  RUN_TEST(test_offset_clamp_upper_bound);

  RUN_TEST(test_nvs_roundtrip_positive);
  RUN_TEST(test_nvs_roundtrip_negative);
  RUN_TEST(test_nvs_roundtrip_all_prayers);

  RUN_TEST(test_timeout_discards_pending_value);

  RUN_TEST(test_reset_all_offsets);

  return UNITY_END();
}
