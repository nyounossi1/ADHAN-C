/*
 * test_AD_39 — Update-available banner "Press OK" hint + FOTA install confirm.
 *
 * Mirrors the pure decision logic in UiTask.cpp (ui_drawArcIdleScreen banner
 * font fallback, and the FOTA install-prompt selection/gating in
 * drawFotaStatusPage / the SCREEN_INFO event handler). No hardware/GFX/WiFi
 * dependencies, so these constants/formulas are duplicated from the source —
 * keep in sync if those change.
 *
 * AC coverage:
 *   AC1  Banner drops to the compact font only when the combined text doesn't
 *        fit the 128x10 banner at the normal 6x8 font.
 *   AC3  Install/Go Back prompt only appears once a check completes with an
 *        update found (not while busy, not when up to date).
 *   AC3  Selection cycles between Install(0) and Go Back(1); OK on Install
 *        only starts a new session if one isn't already running.
 */

#include <unity.h>
#include <string.h>

// ---- Banner geometry (mirrors UiTask.cpp ui_drawArcIdleScreen) ----
static const int BANNER_W          = 128;
static const int BANNER_MARGIN     = 3;
static const int BANNER_AVAIL_W    = BANNER_W - 2 * BANNER_MARGIN;  // 122
static const int BUILTIN_CHAR_W    = 6;  // Adafruit GFX built-in font, size 1

static int builtinTextWidthPx(const char* s) {
  return (int)strlen(s) * BUILTIN_CHAR_W;
}

// true => text doesn't fit at the built-in size, must fall back to TomThumb
static bool bannerNeedsCompactFont(const char* s) {
  return builtinTextWidthPx(s) > BANNER_AVAIL_W;
}

void test_short_banner_text_fits_builtin_font(void) {
  TEST_ASSERT_FALSE(bannerNeedsCompactFont("UPDATE AVAILABLE"));
}

void test_combined_banner_text_falls_back_to_compact_font(void) {
  TEST_ASSERT_TRUE(bannerNeedsCompactFont("UPDATE AVAILABLE - PRESS OK"));
}

// ---- FOTA install-prompt gating (mirrors drawFotaStatusPage / SCREEN_INFO) ----
static bool showInstallPrompt(bool fotaBusy, bool updateAvailable) {
  return !fotaBusy && updateAvailable;
}

void test_prompt_hidden_while_checking(void) {
  TEST_ASSERT_FALSE(showInstallPrompt(/*busy=*/true, /*avail=*/true));
}

void test_prompt_hidden_when_up_to_date(void) {
  TEST_ASSERT_FALSE(showInstallPrompt(/*busy=*/false, /*avail=*/false));
}

void test_prompt_shown_once_check_completes_with_update(void) {
  TEST_ASSERT_TRUE(showInstallPrompt(/*busy=*/false, /*avail=*/true));
}

// ---- Selection cycling (mirrors g_fotaInstallSel = (sel + 1) % 2) ----
static uint8_t nextInstallSel(uint8_t sel) { return (sel + 1) % 2; }

void test_selection_cycles_install_and_go_back(void) {
  uint8_t sel = 0; // Install
  sel = nextInstallSel(sel);
  TEST_ASSERT_EQUAL_UINT8(1, sel); // Go Back
  sel = nextInstallSel(sel);
  TEST_ASSERT_EQUAL_UINT8(0, sel); // back to Install
}

// ---- actInstallUpdate() guard (mirrors UiTask.cpp actInstallUpdate) ----
struct FotaState { bool busy; int installCmdsSent; };

static void actInstallUpdateSim(FotaState& st) {
  if (st.busy) return;
  st.busy = true;
  st.installCmdsSent++;
}

void test_install_action_starts_session_when_idle(void) {
  FotaState st{ false, 0 };
  actInstallUpdateSim(st);
  TEST_ASSERT_TRUE(st.busy);
  TEST_ASSERT_EQUAL_INT(1, st.installCmdsSent);
}

void test_install_action_is_noop_when_already_busy(void) {
  FotaState st{ true, 0 };
  actInstallUpdateSim(st);
  TEST_ASSERT_EQUAL_INT(0, st.installCmdsSent);
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_short_banner_text_fits_builtin_font);
  RUN_TEST(test_combined_banner_text_falls_back_to_compact_font);
  RUN_TEST(test_prompt_hidden_while_checking);
  RUN_TEST(test_prompt_hidden_when_up_to_date);
  RUN_TEST(test_prompt_shown_once_check_completes_with_update);
  RUN_TEST(test_selection_cycles_install_and_go_back);
  RUN_TEST(test_install_action_starts_session_when_idle);
  RUN_TEST(test_install_action_is_noop_when_already_busy);
  return UNITY_END();
}
