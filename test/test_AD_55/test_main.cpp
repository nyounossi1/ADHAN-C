/*
 * test_AD_55 — OLED burn-in protection: pixel shift, brightness, idle indicators.
 *
 * Compiles src/DisplayPolicy.h directly (plain C++).
 *
 * AC coverage:
 *   AC-1  The shift orbit stays within +/-2 px horizontally and +/-1 px vertically,
 *         changes every minute and visits every position; shifting the framebuffer
 *         moves pixels exactly and clears vacated ones.
 *   AC-2  Volume dots show only inside the window after a volume change.
 *   AC-4  NO WIFI shows when Wi-Fi is not set up or the last attempt failed.
 *   AC-6  Dim mode is dim on the home screen and bright while interacting; Bright
 *         mode is always bright (and below maximum contrast).
 */

#include <unity.h>
#include <string.h>
#include "../../src/DisplayPolicy.h"

static const int W = 128, PAGES = 8;
static uint8_t fb[W * PAGES];

void setUp() { memset(fb, 0, sizeof(fb)); }
void tearDown() {}

static void setPx(int x, int y) { fb[(y / 8) * W + x] |= (uint8_t)(1 << (y % 8)); }
static bool getPx(int x, int y) {
  if (x < 0 || x >= W || y < 0 || y >= 64) return false;
  return (fb[(y / 8) * W + x] >> (y % 8)) & 1;
}
static int countPx() { int n = 0; for (int y = 0; y < 64; y++) for (int x = 0; x < W; x++) n += getPx(x, y); return n; }

void test_orbit_bounds_and_cycle() {
  for (int i = 0; i < DP_ORBIT_LEN; i++) {
    TEST_ASSERT_TRUE(DP_ORBIT[i].dx >= -DP_SHIFT_MAX_X && DP_ORBIT[i].dx <= DP_SHIFT_MAX_X);
    TEST_ASSERT_TRUE(DP_ORBIT[i].dy >= -DP_SHIFT_MAX_Y && DP_ORBIT[i].dy <= DP_SHIFT_MAX_Y);
    for (int j = i + 1; j < DP_ORBIT_LEN; j++)    // every position is unique
      TEST_ASSERT_TRUE(DP_ORBIT[i].dx != DP_ORBIT[j].dx || DP_ORBIT[i].dy != DP_ORBIT[j].dy);
  }
  TEST_ASSERT_EQUAL_INT((2 * DP_SHIFT_MAX_X + 1) * (2 * DP_SHIFT_MAX_Y + 1), DP_ORBIT_LEN);  // all 15
  const DpShift a = dpShiftForMinute(100), b = dpShiftForMinute(101);
  TEST_ASSERT_TRUE(a.dx != b.dx || a.dy != b.dy);                 // moves every minute
  TEST_ASSERT_EQUAL_INT8(dpShiftForMinute(7).dx, dpShiftForMinute(7 + DP_ORBIT_LEN).dx);
  TEST_ASSERT_EQUAL_INT8(0, dpShiftForMinute(0).dx);
}

void test_shift_moves_pixels_exactly() {
  setPx(10, 20); setPx(11, 20); setPx(60, 7);
  dpShiftFrame(fb, W, PAGES, 2, 1);
  TEST_ASSERT_TRUE(getPx(12, 21)); TEST_ASSERT_TRUE(getPx(13, 21)); TEST_ASSERT_TRUE(getPx(62, 8));
  TEST_ASSERT_EQUAL_INT(3, countPx());
  dpShiftFrame(fb, W, PAGES, -2, -1);
  TEST_ASSERT_TRUE(getPx(10, 20)); TEST_ASSERT_TRUE(getPx(11, 20)); TEST_ASSERT_TRUE(getPx(60, 7));
  TEST_ASSERT_EQUAL_INT(3, countPx());
}

void test_shift_crosses_page_boundaries() {
  setPx(30, 7); setPx(31, 8);      // last row of page 0, first row of page 1
  dpShiftFrame(fb, W, PAGES, 0, 1);
  TEST_ASSERT_TRUE(getPx(30, 8)); TEST_ASSERT_TRUE(getPx(31, 9));
  dpShiftFrame(fb, W, PAGES, 0, -1);
  TEST_ASSERT_TRUE(getPx(30, 7)); TEST_ASSERT_TRUE(getPx(31, 8));
}

void test_shift_clears_vacated_and_crops_only_outside() {
  for (int x = 0; x < W; x++) setPx(x, 30);                     // full-width line
  dpShiftFrame(fb, W, PAGES, 2, 0);
  TEST_ASSERT_FALSE(getPx(0, 30)); TEST_ASSERT_FALSE(getPx(1, 30));
  TEST_ASSERT_TRUE(getPx(2, 30));  TEST_ASSERT_TRUE(getPx(127, 30));
  TEST_ASSERT_EQUAL_INT(W - 2, countPx());
  // content inside the margins survives every orbit position
  for (int i = 0; i < DP_ORBIT_LEN; i++) {
    memset(fb, 0, sizeof(fb));
    setPx(DP_SHIFT_MAX_X, DP_SHIFT_MAX_Y); setPx(W - 1 - DP_SHIFT_MAX_X, 63 - DP_SHIFT_MAX_Y);
    dpShiftFrame(fb, W, PAGES, DP_ORBIT[i].dx, DP_ORBIT[i].dy);
    TEST_ASSERT_EQUAL_INT(2, countPx());
  }
}

void test_volume_dots_window() {
  TEST_ASSERT_FALSE(dpShowVolumeDots(1000, 0));            // never changed
  TEST_ASSERT_TRUE(dpShowVolumeDots(1000, 6000));
  TEST_ASSERT_TRUE(dpShowVolumeDots(5999, 6000));
  TEST_ASSERT_FALSE(dpShowVolumeDots(6000, 6000));         // window over
  TEST_ASSERT_TRUE(dpShowVolumeDots(0xFFFFFFF0u, 0x10u));  // millis() wrap
}

void test_no_wifi_condition() {
  TEST_ASSERT_FALSE(dpShowNoWifi(false, false));
  TEST_ASSERT_TRUE(dpShowNoWifi(true, false));
  TEST_ASSERT_TRUE(dpShowNoWifi(false, true));
}

void test_contrast_policy() {
  TEST_ASSERT_EQUAL_UINT8(DP_CONTRAST_DIM,    dpContrast(DP_DIM, false));
  TEST_ASSERT_EQUAL_UINT8(DP_CONTRAST_BRIGHT, dpContrast(DP_DIM, true));
  TEST_ASSERT_EQUAL_UINT8(DP_CONTRAST_BRIGHT, dpContrast(DP_BRIGHT, false));
  TEST_ASSERT_EQUAL_UINT8(DP_CONTRAST_BRIGHT, dpContrast(DP_BRIGHT, true));
  TEST_ASSERT_TRUE(DP_CONTRAST_DIM < DP_CONTRAST_BRIGHT);
  TEST_ASSERT_TRUE(DP_CONTRAST_BRIGHT < 0xFF);             // bright is not maximum
  // Auto Dim: bright for 30 s after the last interaction, then dim; interaction/Adhan brightens
  TEST_ASSERT_FALSE(dpIsDim(DP_AUTO_DIM, false, 0));
  TEST_ASSERT_FALSE(dpIsDim(DP_AUTO_DIM, false, DP_AUTO_DIM_MS - 1));
  TEST_ASSERT_TRUE(dpIsDim(DP_AUTO_DIM, false, DP_AUTO_DIM_MS));
  TEST_ASSERT_FALSE(dpIsDim(DP_AUTO_DIM, true, 10 * DP_AUTO_DIM_MS));
  TEST_ASSERT_EQUAL_UINT8(DP_CONTRAST_DIM, dpContrast(DP_AUTO_DIM, false, DP_AUTO_DIM_MS));
  TEST_ASSERT_EQUAL_UINT32(30000, DP_AUTO_DIM_MS);
  TEST_ASSERT_FALSE(dpIsDim(DP_BRIGHT, false, 10 * DP_AUTO_DIM_MS));     // Bright never dims
  TEST_ASSERT_TRUE(dpIsDim(DP_DIM, false, 0));                           // Dim dims at once
  TEST_ASSERT_TRUE(dpIsDim(DP_DIM, false));
  TEST_ASSERT_FALSE(dpIsDim(DP_DIM, true));
  TEST_ASSERT_FALSE(dpIsDim(DP_BRIGHT, false));
  TEST_ASSERT_TRUE(DP_PRECHARGE_DIM != DP_PRECHARGE_BRIGHT && DP_VCOMH_DIM != DP_VCOMH_BRIGHT);  // dim lowers more than contrast
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_orbit_bounds_and_cycle);
  RUN_TEST(test_shift_moves_pixels_exactly);
  RUN_TEST(test_shift_crosses_page_boundaries);
  RUN_TEST(test_shift_clears_vacated_and_crops_only_outside);
  RUN_TEST(test_volume_dots_window);
  RUN_TEST(test_no_wifi_condition);
  RUN_TEST(test_contrast_policy);
  return UNITY_END();
}
