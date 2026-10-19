/*
 * test_AD_50 — Manual location override: query parsing and manual-vs-auto choice.
 *
 * Compiles src/LocationQuery.h directly (plain C++).
 *
 * AC coverage:
 *   AC-1  "latitude, longitude" accepted as an advanced entry; out-of-range rejected.
 *   AC-2  UK postcodes/outcodes go to postcodes.io, other text is a town name;
 *         ambiguous towns prefer the ip-api country; queries are URL-encoded.
 *   AC-3  A resolved manual location always wins; Auto or unresolved uses ip-api.
 */

#include <unity.h>
#include <math.h>
#include "../../src/LocationQuery.h"

void setUp() {}
void tearDown() {}

void test_latlon_parsing() {
  double la = 0, lo = 0;
  TEST_ASSERT_TRUE(lqParseLatLon("51.2625, -1.0871", la, lo));
  TEST_ASSERT_TRUE(fabs(la - 51.2625) < 1e-9);   // Unity double asserts are disabled
  TEST_ASSERT_TRUE(fabs(lo - -1.0871) < 1e-9);
  TEST_ASSERT_TRUE(lqParseLatLon("51.2625 -1.0871", la, lo));
  TEST_ASSERT_TRUE(lqParseLatLon("-33.86,151.21", la, lo));
  TEST_ASSERT_FALSE(lqParseLatLon("91, 0", la, lo));
  TEST_ASSERT_FALSE(lqParseLatLon("51.2, 181", la, lo));
  TEST_ASSERT_FALSE(lqParseLatLon("51.2", la, lo));
  TEST_ASSERT_FALSE(lqParseLatLon("Basingstoke", la, lo));
  TEST_ASSERT_FALSE(lqParseLatLon("51.2, -1.0 extra", la, lo));
}

void test_uk_postcodes() {
  bool full = false; char c[16];
  TEST_ASSERT_TRUE(lqIsUkPostcode("RG21", full, c, sizeof(c)));
  TEST_ASSERT_FALSE(full); TEST_ASSERT_EQUAL_STRING("RG21", c);
  TEST_ASSERT_TRUE(lqIsUkPostcode("rg21 4aa", full, c, sizeof(c)));
  TEST_ASSERT_TRUE(full); TEST_ASSERT_EQUAL_STRING("RG214AA", c);
  TEST_ASSERT_TRUE(lqIsUkPostcode("M1 4AA", full, c, sizeof(c)));   TEST_ASSERT_TRUE(full);
  TEST_ASSERT_TRUE(lqIsUkPostcode("SW1A 1AA", full, c, sizeof(c))); TEST_ASSERT_TRUE(full);
  TEST_ASSERT_TRUE(lqIsUkPostcode("W1A 0AX", full, c, sizeof(c)));  TEST_ASSERT_TRUE(full);
  TEST_ASSERT_TRUE(lqIsUkPostcode("CM3", full, c, sizeof(c)));      TEST_ASSERT_FALSE(full);
  TEST_ASSERT_FALSE(lqIsUkPostcode("Basingstoke", full, c, sizeof(c)));
  TEST_ASSERT_FALSE(lqIsUkPostcode("Bath", full, c, sizeof(c)));
  TEST_ASSERT_FALSE(lqIsUkPostcode("", full, c, sizeof(c)));
}

void test_classify() {
  TEST_ASSERT_EQUAL_INT(LQ_EMPTY,    lqClassify("   "));
  TEST_ASSERT_EQUAL_INT(LQ_LATLON,   lqClassify("51.2625, -1.0871"));
  TEST_ASSERT_EQUAL_INT(LQ_POSTCODE, lqClassify("RG21 4AA"));
  TEST_ASSERT_EQUAL_INT(LQ_TOWN,     lqClassify("Basingstoke"));
  TEST_ASSERT_EQUAL_INT(LQ_TOWN,     lqClassify("Burnham-on-Crouch"));
}

void test_url_encode() {
  char out[64];
  TEST_ASSERT_TRUE(lqUrlEncode("Burnham-on-Crouch", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Burnham-on-Crouch", out);
  TEST_ASSERT_TRUE(lqUrlEncode("St Albans & Co", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("St%20Albans%20%26%20Co", out);
  char tiny[4];
  TEST_ASSERT_FALSE(lqUrlEncode("a b", tiny, sizeof(tiny)));
}

void test_ambiguous_town_prefers_ip_country() {
  const char* cc[] = { "CA", "GB", "US" };
  TEST_ASSERT_EQUAL_INT(1, lqPickResult(cc, 3, "GB"));
  TEST_ASSERT_EQUAL_INT(1, lqPickResult(cc, 3, "gb"));
  TEST_ASSERT_EQUAL_INT(0, lqPickResult(cc, 3, "FR"));   // no match -> first result
  TEST_ASSERT_EQUAL_INT(0, lqPickResult(cc, 3, ""));
  TEST_ASSERT_EQUAL_INT(-1, lqPickResult(cc, 0, "GB"));  // not found
}

void test_manual_location_wins_only_when_resolved() {
  TEST_ASSERT_TRUE(lqUseManual(LOC_MANUAL, true));
  TEST_ASSERT_FALSE(lqUseManual(LOC_MANUAL, false));   // pending lookup -> ip-api
  TEST_ASSERT_FALSE(lqUseManual(LOC_AUTO, true));
  TEST_ASSERT_FALSE(lqUseManual(LOC_AUTO, false));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_latlon_parsing);
  RUN_TEST(test_uk_postcodes);
  RUN_TEST(test_classify);
  RUN_TEST(test_url_encode);
  RUN_TEST(test_ambiguous_town_prefers_ip_country);
  RUN_TEST(test_manual_location_wins_only_when_resolved);
  return UNITY_END();
}
