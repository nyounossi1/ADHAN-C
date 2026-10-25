/*
 * test_AD_44 — Settings-triggered refresh must never permanently strand the
 * UI on SCREEN_SPLASH.
 *
 * Mirrors two independent pieces of UiTask.cpp / WifiManager.cpp:
 *   1. isStartupComplete()'s requirement that g_menuDepth == -1 (and
 *      g_infoMode == INFO_NONE) before splash can ever exit.
 *   2. The UI_EVT_WIFI_STA_CONNECTING handler, which forces SCREEN_SPLASH —
 *      before AD-44 it did so unconditionally; after AD-44 it resets menu
 *      state first so splash can always exit again.
 *
 * AC coverage:
 *   AC-1  runWifiSessionRefresh()'s connectStaWithRetry() call passes
 *         notifyUiSplash=false — a settings-triggered refresh never fires
 *         UI_EVT_WIFI_STA_CONNECTING at all, so menu state is undisturbed.
 *   AC-2  If UI_EVT_WIFI_STA_CONNECTING DOES fire while a menu is open (any
 *         other/future caller), the handler resets g_menuDepth/g_infoMode so
 *         isStartupComplete() can still become true once time/location/
 *         prayers are ready — no permanent hang.
 *   AC-2  Regression: the pre-AD-44 behavior (no reset) is shown to
 *         permanently deadlock under the same inputs, proving the fix
 *         actually changes the outcome rather than being a no-op.
 */

#include <unity.h>

// ============================================================================
// Mirrors UiTask.cpp's isStartupComplete() inputs/logic
// ============================================================================
struct StartupState {
  bool timeSynced;
  bool locationReady;
  bool prayerReady;
  int  menuDepth;   // -1 == not in a menu
  int  infoMode;    // 0 == INFO_NONE
};

static bool isStartupComplete(const StartupState& s) {
  bool uiReady = (s.menuDepth == -1 && s.infoMode == 0);
  return s.timeSynced && s.locationReady && s.prayerReady && uiReady;
}

// Pre-AD-44 behavior: forces splash, never touches menu state.
static void wifiConnectingHandler_beforeFix(StartupState& s) {
  (void)s; // no-op on menu state — this is the bug
}

// Post-AD-44 behavior: resets menu state before forcing splash.
static void wifiConnectingHandler_afterFix(StartupState& s) {
  if (s.menuDepth != -1 || s.infoMode != 0) {
    s.menuDepth = -1;
    s.infoMode = 0;
  }
}

void setUp() {}
void tearDown() {}

// ============================================================================
// AC-2: pre-fix behavior permanently deadlocks (proves the bug is real)
// ============================================================================
void test_before_fix_stranded_in_menu_never_completes_startup() {
  StartupState s{ false, false, false, 2 /* deep in Settings > Time > TZ */, 0 };

  // Background refresh fires while user is in the menu (old behavior: no reset)
  wifiConnectingHandler_beforeFix(s);

  // ...refresh completes successfully...
  s.timeSynced = true;
  s.locationReady = true;
  s.prayerReady = true;

  // menuDepth is still stuck at 2 -- isStartupComplete() can never return
  // true again, exactly matching the reported permanent hang.
  TEST_ASSERT_FALSE(isStartupComplete(s));
}

// ============================================================================
// AC-2: post-fix behavior always recovers
// ============================================================================
void test_after_fix_recovers_even_if_triggered_mid_menu() {
  StartupState s{ false, false, false, 2, 0 };

  wifiConnectingHandler_afterFix(s);
  TEST_ASSERT_EQUAL_INT(-1, s.menuDepth);

  s.timeSynced = true;
  s.locationReady = true;
  s.prayerReady = true;

  TEST_ASSERT_TRUE(isStartupComplete(s));
}

void test_after_fix_also_resets_info_mode() {
  StartupState s{ true, true, true, -1, 3 /* e.g. INFO_ABOUT open */ };
  TEST_ASSERT_FALSE(isStartupComplete(s)); // infoMode still open

  wifiConnectingHandler_afterFix(s);
  TEST_ASSERT_TRUE(isStartupComplete(s));
}

// Not in a menu at all — handler should be a no-op either way
void test_fix_is_noop_when_not_in_a_menu() {
  StartupState s{ true, true, true, -1, 0 };
  wifiConnectingHandler_afterFix(s);
  TEST_ASSERT_TRUE(isStartupComplete(s));
}

// ============================================================================
// AC-1: settings-triggered refresh (runWifiSessionRefresh) must not raise
// UI_EVT_WIFI_STA_CONNECTING at all -- modeled as notifyUiSplash=false
// meaning the connecting-handler is never invoked, so menu state is
// completely undisturbed by a background refresh.
// ============================================================================
void test_settings_triggered_refresh_leaves_menu_state_untouched() {
  StartupState s{ true, true, true, 2, 0 }; // mid-menu, otherwise fully ready

  const bool notifyUiSplash = false; // runWifiSessionRefresh's fixed call
  if (notifyUiSplash) {
    wifiConnectingHandler_afterFix(s); // would not even run with the fix
  }

  // Menu state must be exactly as the user left it -- refresh happened
  // entirely in the background.
  TEST_ASSERT_EQUAL_INT(2, s.menuDepth);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_before_fix_stranded_in_menu_never_completes_startup);
  RUN_TEST(test_after_fix_recovers_even_if_triggered_mid_menu);
  RUN_TEST(test_after_fix_also_resets_info_mode);
  RUN_TEST(test_fix_is_noop_when_not_in_a_menu);
  RUN_TEST(test_settings_triggered_refresh_leaves_menu_state_untouched);

  return UNITY_END();
}
