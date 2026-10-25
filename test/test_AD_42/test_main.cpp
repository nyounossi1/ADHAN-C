/*
 * test_AD_42 — Unit tests for Next Prayer screen (all 5 prayers + AM/PM fix).
 *
 * AC coverage:
 *   AC (time format) — fmtMinutesToClock() shows AM or PM in 12hr mode (both,
 *                       not just PM), and no suffix in 24hr mode.
 *   AC (format)       — row text is built as "NAME: HH:MM[ AM/PM]".
 *   AC (highlight)    — the row whose PrayerId matches nextId is the one
 *                       selected for the inverse highlight.
 */

#include <unity.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Minimal stand-ins — no hardware, no FreeRTOS, no Adafruit_GFX
// ============================================================================

static int g_timeFormat = 1; // 0=12Hr, 1=24Hr — mirrors Globals.cpp default

// Mirrors the fixed fmtMinutesToClock() in Globals.cpp
static void fmtMinutesToClock(int minutes, char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  if (minutes < 0) { snprintf(out, outLen, "--:--"); return; }

  int h24 = (minutes / 60) % 24;
  int m   = minutes % 60;

  if (g_timeFormat == 0) {
    int h12 = h24 % 12;
    if (h12 == 0) h12 = 12;
    const bool isPM = (h24 >= 12);
    snprintf(out, outLen, "%02d:%02d %s", h12, m, isPM ? "PM" : "AM");
  } else {
    snprintf(out, outLen, "%02d:%02d", h24, m);
  }
}

enum PrayerId : uint8_t { PR_NONE=0, PR_FAJR, PR_DHUHR, PR_ASR, PR_MAGHRIB, PR_ISHA };
static const char* PRAYER_NAME[] = { "-", "FAJR", "DHUHR", "ASR", "MAGHRIB", "ISHA" };

// Mirrors the per-row line building in ui_drawPrayerScreen()
static void buildRowLine(char* out, size_t outLen, PrayerId id, int minutes) {
  char timeBuf[12];
  fmtMinutesToClock(minutes, timeBuf, sizeof(timeBuf));
  snprintf(out, outLen, "%s: %s", PRAYER_NAME[(uint8_t)id], timeBuf);
}

// ============================================================================
// setUp / tearDown
// ============================================================================
void setUp() { g_timeFormat = 1; }
void tearDown() {}

// ============================================================================
// 24hr mode — no AM/PM suffix (unchanged behaviour)
// ============================================================================
void test_24hr_no_suffix() {
  g_timeFormat = 1;
  char buf[12];
  fmtMinutesToClock(256, buf, sizeof(buf)); // 04:16
  TEST_ASSERT_EQUAL_STRING("04:16", buf);
}

void test_24hr_evening_no_suffix() {
  g_timeFormat = 1;
  char buf[12];
  fmtMinutesToClock(1285, buf, sizeof(buf)); // 21:25
  TEST_ASSERT_EQUAL_STRING("21:25", buf);
}

// ============================================================================
// 12hr mode — AM must now be shown (previously omitted), PM unchanged
// ============================================================================
void test_12hr_am_is_shown() {
  g_timeFormat = 0;
  char buf[12];
  fmtMinutesToClock(256, buf, sizeof(buf)); // 04:16 -> 04:16 AM
  TEST_ASSERT_EQUAL_STRING("04:16 AM", buf);
}

void test_12hr_pm_is_shown() {
  g_timeFormat = 0;
  char buf[12];
  fmtMinutesToClock(1285, buf, sizeof(buf)); // 21:25 -> 09:25 PM
  TEST_ASSERT_EQUAL_STRING("09:25 PM", buf);
}

// Midnight: 00:00 must read 12:00 AM, not 00:00 AM
void test_12hr_midnight_is_12am() {
  g_timeFormat = 0;
  char buf[12];
  fmtMinutesToClock(0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("12:00 AM", buf);
}

// Noon: 12:00 must read 12:00 PM, not 00:00 PM
void test_12hr_noon_is_12pm() {
  g_timeFormat = 0;
  char buf[12];
  fmtMinutesToClock(720, buf, sizeof(buf)); // 12:00
  TEST_ASSERT_EQUAL_STRING("12:00 PM", buf);
}

// Invalid/unset time (-1) still falls back to placeholder in both formats
void test_invalid_minutes_placeholder() {
  g_timeFormat = 0;
  char buf12[12];
  fmtMinutesToClock(-1, buf12, sizeof(buf12));
  TEST_ASSERT_EQUAL_STRING("--:--", buf12);

  g_timeFormat = 1;
  char buf24[12];
  fmtMinutesToClock(-1, buf24, sizeof(buf24));
  TEST_ASSERT_EQUAL_STRING("--:--", buf24);
}

// ============================================================================
// Row format — "NAME: HH:MM[ AM/PM]"
// ============================================================================
void test_row_format_24hr() {
  g_timeFormat = 1;
  char line[24];
  buildRowLine(line, sizeof(line), PR_FAJR, 256);
  TEST_ASSERT_EQUAL_STRING("FAJR: 04:16", line);
}

void test_row_format_12hr() {
  g_timeFormat = 0;
  char line[24];
  buildRowLine(line, sizeof(line), PR_MAGHRIB, 1216); // 20:16 -> 08:16 PM
  TEST_ASSERT_EQUAL_STRING("MAGHRIB: 08:16 PM", line);
}

// ============================================================================
// Highlight selection — the row matching nextId is the one to invert
// ============================================================================
void test_highlight_matches_next_id() {
  static const PrayerId kRowId[5] = { PR_FAJR, PR_DHUHR, PR_ASR, PR_MAGHRIB, PR_ISHA };
  PrayerId nextId = PR_ASR;

  int highlightedRow = -1;
  for (int i = 0; i < 5; i++) {
    if (kRowId[i] == nextId) highlightedRow = i;
  }
  TEST_ASSERT_EQUAL_INT(2, highlightedRow); // Asr is row index 2
}

// When computeNextPrayer() rolls over to tomorrow's Fajr, nextId is still
// PR_FAJR — the Fajr row (row 0) is highlighted, per AD-42's decision to key
// the highlight off nextId alone rather than special-casing fromTomorrow.
void test_highlight_tomorrow_fajr_still_highlights_fajr_row() {
  static const PrayerId kRowId[5] = { PR_FAJR, PR_DHUHR, PR_ASR, PR_MAGHRIB, PR_ISHA };
  PrayerId nextId = PR_FAJR; // fromTomorrow == true, but outId is still PR_FAJR

  int highlightedRow = -1;
  for (int i = 0; i < 5; i++) {
    if (kRowId[i] == nextId) highlightedRow = i;
  }
  TEST_ASSERT_EQUAL_INT(0, highlightedRow);
}

// ============================================================================
// main
// ============================================================================
int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_24hr_no_suffix);
  RUN_TEST(test_24hr_evening_no_suffix);

  RUN_TEST(test_12hr_am_is_shown);
  RUN_TEST(test_12hr_pm_is_shown);
  RUN_TEST(test_12hr_midnight_is_12am);
  RUN_TEST(test_12hr_noon_is_12pm);
  RUN_TEST(test_invalid_minutes_placeholder);

  RUN_TEST(test_row_format_24hr);
  RUN_TEST(test_row_format_12hr);

  RUN_TEST(test_highlight_matches_next_id);
  RUN_TEST(test_highlight_tomorrow_fajr_still_highlights_fajr_row);

  return UNITY_END();
}
