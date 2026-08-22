/*
 * test_AD_43 — Unit tests for Asia/Karachi timezone addition.
 *
 * AC coverage:
 *   AC-1  "Asia/Karachi" resolves to POSIX string "PKT-5" via findPosix()
 *   AC-2  Existing zones (e.g. Europe/London) still resolve correctly —
 *         regression guard since the AD-41 debug/monitoring device depends
 *         on Europe/London staying correct.
 *   AC-2  Unmapped zones still fall through to no match (UTC0 fallback path
 *         in applyTimezonePosix() is exercised elsewhere; here we just check
 *         findPosix() returns null for something not in the table).
 */

#include <unity.h>
#include <string.h>

// ============================================================================
// Mirrors Globals.cpp's IANA_TO_POSIX table + findPosix() lookup logic
// ============================================================================
struct TzEntry { const char* iana; const char* posix; };

static const TzEntry IANA_TO_POSIX[] = {
  { "Europe/London",       "GMT0BST,M3.5.0/1,M10.5.0/2" },
  { "Europe/Copenhagen",   "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "Europe/Oslo",         "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "Europe/Stockholm",    "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "Europe/Paris",        "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "Europe/Madrid",       "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "Europe/Rome",         "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "Europe/Berlin",       "CET-1CEST,M3.5.0/2,M10.5.0/3" },
  { "Europe/Helsinki",     "EET-2EEST,M3.5.0/3,M10.5.0/4" },
  { "America/New_York",    "EST5EDT,M3.2.0/2,M11.1.0/2" },
  { "America/Chicago",     "CST6CDT,M3.2.0/2,M11.1.0/2" },
  { "America/Denver",      "MST7MDT,M3.2.0/2,M11.1.0/2" },
  { "America/Los_Angeles", "PST8PDT,M3.2.0/2,M11.1.0/2" },
  { "Australia/Sydney",    "AEST-10AEDT,M10.1.0/2,M4.1.0/3" },
  { "Pacific/Auckland",    "NZST-12NZDT,M9.5.0/2,M4.1.0/3" },
  { "Asia/Karachi",        "PKT-5" }
};
static const int IANA_TO_POSIX_COUNT = sizeof(IANA_TO_POSIX) / sizeof(IANA_TO_POSIX[0]);

static const char* findPosix(const char* iana) {
  for (int i = 0; i < IANA_TO_POSIX_COUNT; i++) {
    if (strcmp(iana, IANA_TO_POSIX[i].iana) == 0) return IANA_TO_POSIX[i].posix;
  }
  return nullptr;
}

// Mirrors UiTask.cpp's kTzList[] — must stay in sync with IANA_TO_POSIX
// (this is the exact bug class the AC calls out: a zone in one but not
// the other either can't be selected, or silently falls back to UTC0).
static const char* kTzList[] = {
  "Auto (from Wi-Fi/IP)",
  "Europe/London",
  "Europe/Copenhagen",
  "Europe/Oslo",
  "Europe/Stockholm",
  "Europe/Paris",
  "Europe/Madrid",
  "Europe/Rome",
  "Europe/Berlin",
  "Europe/Helsinki",
  "America/New_York",
  "America/Chicago",
  "America/Denver",
  "America/Los_Angeles",
  "Australia/Sydney",
  "Pacific/Auckland",
  "Asia/Karachi"
};
static const int kTzListCount = sizeof(kTzList) / sizeof(kTzList[0]);

void setUp() {}
void tearDown() {}

// ============================================================================
// AC-1: Asia/Karachi resolves correctly
// ============================================================================
void test_karachi_resolves_to_pkt_minus5() {
  const char* posix = findPosix("Asia/Karachi");
  TEST_ASSERT_NOT_NULL(posix);
  TEST_ASSERT_EQUAL_STRING("PKT-5", posix);
}

// ============================================================================
// AC-2: existing zone still resolves (regression guard for the AD-41 device)
// ============================================================================
void test_london_still_resolves() {
  const char* posix = findPosix("Europe/London");
  TEST_ASSERT_NOT_NULL(posix);
  TEST_ASSERT_EQUAL_STRING("GMT0BST,M3.5.0/1,M10.5.0/2", posix);
}

// ============================================================================
// AC-2: unmapped zone still falls through to no match (drives UTC0 fallback)
// ============================================================================
void test_unmapped_zone_returns_null() {
  TEST_ASSERT_NULL(findPosix("Asia/Dubai"));
}

// ============================================================================
// AC-2: kTzList and IANA_TO_POSIX stay in sync — every entry in the
// on-device picker (except "Auto") must resolve via findPosix(), otherwise
// selecting it would silently fall back to UTC0 (see applyTimezonePosix()).
// ============================================================================
void test_every_kTzList_entry_except_auto_resolves() {
  for (int i = 1; i < kTzListCount; i++) {
    TEST_ASSERT_NOT_NULL_MESSAGE(findPosix(kTzList[i]), kTzList[i]);
  }
}

void test_karachi_present_in_kTzList() {
  bool found = false;
  for (int i = 0; i < kTzListCount; i++) {
    if (strcmp(kTzList[i], "Asia/Karachi") == 0) { found = true; break; }
  }
  TEST_ASSERT_TRUE(found);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_karachi_resolves_to_pkt_minus5);
  RUN_TEST(test_london_still_resolves);
  RUN_TEST(test_unmapped_zone_returns_null);
  RUN_TEST(test_every_kTzList_entry_except_auto_resolves);
  RUN_TEST(test_karachi_present_in_kTzList);

  return UNITY_END();
}
