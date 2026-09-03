/*
 * test_AD_45 — Volume indicator dots: circle -> square shape change.
 *
 * Mirrors the geometry/branching in drawLevelDots3Left()/drawLevelDots3Right()
 * (src/UiTask.cpp). No hardware/Adafruit GFX dependency — a tiny mock
 * "canvas" records which primitive (circle/rect, filled/outline) each dot
 * would draw and at what coordinates, matching the real function's logic.
 *
 * AC coverage:
 *   AC-1  Volume dots render as squares instead of circles (asSquare=true)
 *   AC-2  Square side length equals levelDotsR()
 *   AC-3  Dot spacing/position/group width is unchanged from the circle layout
 *   AC-4  Filled vs. outline state (active vs inactive level) is preserved
 *   AC-5  WiFi call site (asSquare=false, the default) still renders circles
 */

#include <unity.h>

// ============================================================================
// Mirrors of the geometry helpers in UiTask.cpp (single source of truth there)
// ============================================================================
static inline int levelDotsR()   { return 2; }
static inline int levelDotsGap() { return 4; }

static inline int levelDotsWidthPx3() {
  const int R = levelDotsR();
  const int GAP = levelDotsGap();
  const int N = 3;
  return N * (2 * R) + (N - 1) * GAP;
}

// ---- Mock canvas: records one op per dot ----
enum ShapeKind { SHAPE_CIRCLE, SHAPE_RECT };

struct ShapeOp {
  ShapeKind kind;
  int x, y;      // circle: center. rect: top-left corner.
  int size;      // circle: radius. rect: side length.
  bool filled;
};

static ShapeOp g_ops[3];
static int     g_opCount = 0;

static void mockFillCircle(int x, int y, int r) { g_ops[g_opCount++] = { SHAPE_CIRCLE, x, y, r, true }; }
static void mockDrawCircle(int x, int y, int r) { g_ops[g_opCount++] = { SHAPE_CIRCLE, x, y, r, false }; }
static void mockFillRect(int x, int y, int side) { g_ops[g_opCount++] = { SHAPE_RECT, x, y, side, true }; }
static void mockDrawRect(int x, int y, int side) { g_ops[g_opCount++] = { SHAPE_RECT, x, y, side, false }; }

// Mirrors drawLevelDots3Left() from UiTask.cpp
static void drawLevelDots3Left(int xLeft, int y, int level, bool asSquare = false) {
  const int R = levelDotsR();
  const int GAP = levelDotsGap();
  const int STEP = 2 * R + GAP;

  for (int i = 0; i < 3; i++) {
    int cx = xLeft + R + i * STEP;
    if (asSquare) {
      const int side = R; // square side = circle radius
      const int half = side / 2;
      if (i < level) mockFillRect(cx - half, y - half, side);
      else            mockDrawRect(cx - half, y - half, side);
    } else {
      if (i < level) mockFillCircle(cx, y, R);
      else            mockDrawCircle(cx, y, R);
    }
  }
}

static void drawLevelDots3Right(int xRight, int y, int level, bool asSquare = false) {
  drawLevelDots3Left(xRight - levelDotsWidthPx3() - 1, y, level, asSquare);
}

// ============================================================================
// setUp / tearDown
// ============================================================================
void setUp() {
  g_opCount = 0;
  for (int i = 0; i < 3; i++) g_ops[i] = {};
}
void tearDown() {}

// ============================================================================
// AC-1: asSquare=true renders squares, not circles
// ============================================================================
void test_volume_dots_render_as_squares() {
  drawLevelDots3Left(0, 10, 2, true);
  TEST_ASSERT_EQUAL_INT(3, g_opCount);
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_EQUAL_INT(SHAPE_RECT, g_ops[i].kind);
  }
}

// ============================================================================
// AC-2: square side length equals levelDotsR()
// ============================================================================
void test_square_side_equals_radius() {
  drawLevelDots3Left(0, 10, 1, true);
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_EQUAL_INT(levelDotsR(), g_ops[i].size);
  }
}

// ============================================================================
// AC-3: dot centers/spacing are identical between circle and square modes
// ============================================================================
void test_dot_positions_unchanged_between_shapes() {
  drawLevelDots3Left(5, 20, 3, false); // circles
  ShapeOp circleOps[3];
  for (int i = 0; i < 3; i++) circleOps[i] = g_ops[i];

  setUp();
  drawLevelDots3Left(5, 20, 3, true); // squares
  const int half = levelDotsR() / 2;
  for (int i = 0; i < 3; i++) {
    // circle center (cx, y) must equal square center (x+half, y+half)
    TEST_ASSERT_EQUAL_INT(circleOps[i].x, g_ops[i].x + half);
    TEST_ASSERT_EQUAL_INT(circleOps[i].y, g_ops[i].y + half);
  }
}

void test_group_width_unchanged() {
  // drawLevelDots3Right must anchor the group at the same dot CENTERS
  // regardless of shape, since levelDotsWidthPx3() does not depend on
  // asSquare — only the primitive drawn at each center changes.
  drawLevelDots3Right(128, 2, 2, false);
  int centerXCircle = g_ops[0].x; // circle op x is already the center

  setUp();
  drawLevelDots3Right(128, 2, 2, true);
  const int half = levelDotsR() / 2;
  int centerXSquare = g_ops[0].x + half; // rect x is the left edge

  TEST_ASSERT_EQUAL_INT(centerXCircle, centerXSquare);
}

// ============================================================================
// AC-4: filled (active) vs outline (inactive) preserved per level
// ============================================================================
void test_filled_vs_outline_matches_level_square() {
  drawLevelDots3Left(0, 10, 2, true); // level=2 -> dots 0,1 filled, dot 2 outline
  TEST_ASSERT_TRUE(g_ops[0].filled);
  TEST_ASSERT_TRUE(g_ops[1].filled);
  TEST_ASSERT_FALSE(g_ops[2].filled);
}

void test_filled_vs_outline_matches_level_circle() {
  drawLevelDots3Left(0, 10, 1, false); // level=1 -> dot 0 filled, dots 1,2 outline
  TEST_ASSERT_TRUE(g_ops[0].filled);
  TEST_ASSERT_FALSE(g_ops[1].filled);
  TEST_ASSERT_FALSE(g_ops[2].filled);
}

// ============================================================================
// AC-5: default (no asSquare arg) keeps circle shape — WiFi call site
// ============================================================================
void test_default_shape_is_circle() {
  drawLevelDots3Left(0, 10, 3); // no 4th arg, as used by the WiFi call site
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_EQUAL_INT(SHAPE_CIRCLE, g_ops[i].kind);
  }
}

// ============================================================================
// main
// ============================================================================
int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_volume_dots_render_as_squares);
  RUN_TEST(test_square_side_equals_radius);
  RUN_TEST(test_dot_positions_unchanged_between_shapes);
  RUN_TEST(test_group_width_unchanged);
  RUN_TEST(test_filled_vs_outline_matches_level_square);
  RUN_TEST(test_filled_vs_outline_matches_level_circle);
  RUN_TEST(test_default_shape_is_circle);

  return UNITY_END();
}
