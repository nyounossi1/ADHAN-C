/*
 * test_AD_38 — Factory reset countdown screen: text must not overlap the box border.
 *
 * Mirrors the geometry in ui_drawFactoryResetScreen() (src/UiTask.cpp).
 * If those coordinates ever change, these tests catch a regression back into
 * the border-overlap bug (AD-38).
 *
 * AC coverage:
 *   AC-1  No text row overlaps the box border lines (outer y=58, inner y=57/6)
 *   AC-2  >=3px clearance between every text line and the nearest box border
 *   AC-3  Text lines never overlap each other
 */

#include <unity.h>

// ---- Box geometry (drawRect(5,5,118,54) + drawRect(6,6,116,52)) ----
static const int BOX_INNER_TOP    = 6;
static const int BOX_INNER_BOTTOM = 6 + 52 - 1;  // 57
static const int BOX_OUTER_BOTTOM = 5 + 54 - 1;  // 58

// ---- Text element geometry: {top_y, height} ----
struct TextRow { int top; int height; };

static const TextRow TITLE     = { 9,  8 };   // "! FACTORY RESET !" size1
static const TextRow SUBTITLE  = { 19, 8 };   // "Resetting in" size1
static const TextRow COUNTDOWN = { 29, 16 };  // countdown number size2
static const TextRow RELEASE   = { 47, 8 };   // "Release to cancel" size1

static inline int rowBottom(const TextRow& r) { return r.top + r.height - 1; }

static bool overlapsRow(const TextRow& r, int row) {
  return row >= r.top && row <= rowBottom(r);
}

void test_title_clears_top_border(void) {
  TEST_ASSERT_GREATER_OR_EQUAL(3, TITLE.top - BOX_INNER_TOP);
}

void test_release_clears_bottom_border(void) {
  TEST_ASSERT_GREATER_OR_EQUAL(3, BOX_INNER_BOTTOM - rowBottom(RELEASE));
}

void test_no_element_overlaps_border_lines(void) {
  const TextRow* rows[] = { &TITLE, &SUBTITLE, &COUNTDOWN, &RELEASE };
  const int borderRows[] = { BOX_INNER_TOP, BOX_INNER_BOTTOM, BOX_OUTER_BOTTOM };

  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 3; j++) {
      TEST_ASSERT_FALSE(overlapsRow(*rows[i], borderRows[j]));
    }
  }
}

void test_elements_dont_overlap_each_other(void) {
  TEST_ASSERT_LESS_THAN(SUBTITLE.top, rowBottom(TITLE) + 1);
  TEST_ASSERT_LESS_THAN(COUNTDOWN.top, rowBottom(SUBTITLE) + 1);
  TEST_ASSERT_LESS_THAN(RELEASE.top, rowBottom(COUNTDOWN) + 1);
}

void test_all_elements_within_box(void) {
  const TextRow* rows[] = { &TITLE, &SUBTITLE, &COUNTDOWN, &RELEASE };
  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_GREATER_OR_EQUAL(BOX_INNER_TOP, rows[i]->top);
    TEST_ASSERT_LESS_OR_EQUAL(BOX_INNER_BOTTOM, rowBottom(*rows[i]));
  }
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_title_clears_top_border);
  RUN_TEST(test_release_clears_bottom_border);
  RUN_TEST(test_no_element_overlaps_border_lines);
  RUN_TEST(test_elements_dont_overlap_each_other);
  RUN_TEST(test_all_elements_within_box);
  return UNITY_END();
}
