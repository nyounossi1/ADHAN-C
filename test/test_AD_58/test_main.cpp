/*
 * test_AD_58 — getting online again by itself (AD-58 location fallback, AD-59 slow router).
 *
 * Compiles src/WifiRecovery.h directly (plain C++).
 *
 * AC coverage:
 *   AD-58 AC-1  A cached location is used when no provider answers
 *   AD-58 AC-2  The fallback provider is tried when ip-api fails (source order)
 *   AD-58 AC-3  Missing time / location / prayers keep the retry going
 *   AD-58 AC-4  Nothing usable (no provider, no cache) is reported as no source
 *   AD-59 AC-1  A connect failure never opens the portal or erases credentials while they exist
 *   AD-59 AC-2  Backoff 30 s -> 5 min, kept forever, millis() wrap safe
 *   AD-59 AC-3  Router back after minutes or hours: recovered within one backoff step, no reboot
 *   AD-59 AC-4  Portal only without credentials or when the user asks
 */

#include <unity.h>
#include <stdint.h>
#include "../../src/WifiRecovery.h"

void setUp() {}
void tearDown() {}

// ---- AD-59 backoff -------------------------------------------------------------------------
void test_backoff_schedule_and_cap() {
  TEST_ASSERT_EQUAL_UINT32(30000, wrBackoffMs(0));
  TEST_ASSERT_EQUAL_UINT32(60000, wrBackoffMs(1));
  TEST_ASSERT_EQUAL_UINT32(120000, wrBackoffMs(2));
  TEST_ASSERT_EQUAL_UINT32(240000, wrBackoffMs(3));
  TEST_ASSERT_EQUAL_UINT32(300000, wrBackoffMs(4));
  TEST_ASSERT_EQUAL_UINT32(300000, wrBackoffMs(5));
  TEST_ASSERT_EQUAL_UINT32(300000, wrBackoffMs(100000));
}

void test_due_is_wrap_safe() {
  TEST_ASSERT_FALSE(wrDue(29999, 0, 0));
  TEST_ASSERT_TRUE(wrDue(30000, 0, 0));
  const uint32_t last = 0xFFFFFFF0u;                    // 16 ms before millis() wraps
  TEST_ASSERT_FALSE(wrDue(5, last, 0));                 // 21 ms elapsed
  TEST_ASSERT_TRUE(wrDue(30000 - 16, last, 0));         // 30000 ms elapsed across the wrap
}

// ---- AD-58 AC-3 ---------------------------------------------------------------------------
void test_needs_recovery_until_everything_is_ready() {
  TEST_ASSERT_FALSE(wrNeedsRecovery(true, true, true, true));
  TEST_ASSERT_TRUE(wrNeedsRecovery(false, true, true, true));   // time
  TEST_ASSERT_TRUE(wrNeedsRecovery(true, false, true, true));   // time zone
  TEST_ASSERT_TRUE(wrNeedsRecovery(true, true, false, true));   // location
  TEST_ASSERT_TRUE(wrNeedsRecovery(true, true, true, false));   // prayers
  TEST_ASSERT_TRUE(wrNeedsRecovery(false, false, false, false));
}

// ---- AD-59 AC-1 / AC-4 ----------------------------------------------------------------------
void test_portal_rules() {
  TEST_ASSERT_FALSE(wrOpenPortal(true, false));    // credentials saved, connect failed: keep retrying
  TEST_ASSERT_TRUE(wrOpenPortal(false, false));    // first setup
  TEST_ASSERT_TRUE(wrOpenPortal(true, true));      // user asked from the menu / typed new details
}

// ---- AD-58 AC-1 / AC-2 / AC-4 -------------------------------------------------------------
void test_location_source_order() {
  TEST_ASSERT_EQUAL(LS_IPAPI, lsChoose(true, true, true));
  TEST_ASSERT_EQUAL(LS_IPWHO, lsChoose(false, true, true));
  TEST_ASSERT_EQUAL(LS_CACHE, lsChoose(false, false, true));
  TEST_ASSERT_EQUAL(LS_NONE,  lsChoose(false, false, false));
}

void test_cache_validity() {
  TEST_ASSERT_TRUE(lsCacheValid(51.2625, -1.0871, "Europe/London"));
  TEST_ASSERT_FALSE(lsCacheValid(0.0, 0.0, "Europe/London"));   // unset
  TEST_ASSERT_FALSE(lsCacheValid(51.2, -1.0, ""));
  TEST_ASSERT_FALSE(lsCacheValid(51.2, -1.0, nullptr));
  TEST_ASSERT_FALSE(lsCacheValid(91.0, 0.0, "UTC"));
  TEST_ASSERT_FALSE(lsCacheValid(10.0, 181.0, "UTC"));
  TEST_ASSERT_TRUE(lsCacheValid(0.0, 30.0, "Africa/Cairo"));    // only exactly 0,0 means unset
}

void test_cache_saved_only_when_it_changes() {
  TEST_ASSERT_TRUE(lsCacheShouldSave(false, 0, 0, "", 51.2625, -1.0871, "Europe/London"));      // first
  TEST_ASSERT_FALSE(lsCacheShouldSave(true, 51.2625, -1.0871, "Europe/London",
                                      51.2626, -1.0872, "Europe/London"));                      // same place
  TEST_ASSERT_TRUE(lsCacheShouldSave(true, 51.2625, -1.0871, "Europe/London",
                                     51.4402, -0.0259, "Europe/London"));                       // moved
  TEST_ASSERT_TRUE(lsCacheShouldSave(true, 51.2625, -1.0871, "Europe/London",
                                     51.2625, -1.0871, "Europe/Paris"));                        // zone changed
  TEST_ASSERT_FALSE(lsCacheShouldSave(true, 51.2625, -1.0871, "Europe/London", 0.0, 0.0, "UTC")); // junk never saved
}

// ---- AD-59 AC-3: power cut, router comes back after N seconds -----------------------------------
struct Sim { uint32_t attempts; uint32_t recoveredAtS; uint32_t tries; };

// 1 s ticks like the Wi-Fi task. A connect attempt blocks for 45 s (3 x 15 s) when it fails.
static Sim powerCut(uint32_t routerBackAtS, uint32_t horizonS) {
  Sim s = {0, 0, 0};
  bool ready = false;
  uint32_t lastMs = 0;          // millis() at the end of boot
  for (uint32_t t = 0; t < horizonS && !ready; t++) {
    if (wrDue(t * 1000u, lastMs, s.attempts)) {
      s.tries++;
      if (t >= routerBackAtS) { ready = true; s.recoveredAtS = t; }
      else { t += 45; s.attempts++; lastMs = t * 1000u; }            // failed attempt took 45 s
    }
  }
  return s;
}

void test_router_back_after_two_minutes() {
  Sim s = powerCut(120, 3600);
  TEST_ASSERT_TRUE(s.recoveredAtS >= 120);
  TEST_ASSERT_TRUE(s.recoveredAtS <= 120 + 300 + 45);   // at most one backoff step after it returns
}

void test_router_back_after_five_minutes_and_after_three_hours() {
  Sim a = powerCut(300, 7200);
  TEST_ASSERT_TRUE(a.recoveredAtS >= 300 && a.recoveredAtS <= 300 + 300 + 45);
  Sim b = powerCut(3 * 3600, 6 * 3600);
  TEST_ASSERT_TRUE(b.recoveredAtS >= 3 * 3600 && b.recoveredAtS <= 3 * 3600 + 300 + 45);
}

void test_retry_effort_is_bounded_while_the_router_stays_down() {
  Sim s = powerCut(1000000, 86400);                      // down for the whole day
  TEST_ASSERT_EQUAL_UINT32(0, s.recoveredAtS);
  TEST_ASSERT_TRUE(s.tries < 320);                       // ~ one try per 5 min after the first few
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_backoff_schedule_and_cap);
  RUN_TEST(test_due_is_wrap_safe);
  RUN_TEST(test_needs_recovery_until_everything_is_ready);
  RUN_TEST(test_portal_rules);
  RUN_TEST(test_location_source_order);
  RUN_TEST(test_cache_validity);
  RUN_TEST(test_cache_saved_only_when_it_changes);
  RUN_TEST(test_router_back_after_two_minutes);
  RUN_TEST(test_router_back_after_five_minutes_and_after_three_hours);
  RUN_TEST(test_retry_effort_is_bounded_while_the_router_stays_down);
  return UNITY_END();
}
