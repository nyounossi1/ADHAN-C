/*
 * DisplayPolicy.h — OLED burn-in protection helpers (AD-55).
 *
 * Header-only and plain C++ so test_AD_55 can check it natively.
 *   - pixel shift orbit and a framebuffer shifter (SSD1306 page layout)
 *   - Dim / Bright contrast decision
 *   - when the volume dots and the NO WIFI text are shown
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---- Pixel shift -----------------------------------------------------------
// Offsets stay within +/-2 px horizontally and +/-1 px vertically; the idle artwork
// keeps that much margin so nothing is cropped. One step per minute, 15-minute cycle.
static constexpr int DP_SHIFT_MAX_X = 2;
static constexpr int DP_SHIFT_MAX_Y = 1;

struct DpShift { int8_t dx, dy; };

static const DpShift DP_ORBIT[] = {
  { 0, 0}, { 1, 0}, { 2, 0},
  { 2, 1}, { 1, 1}, { 0, 1}, {-1, 1}, {-2, 1},
  {-2, 0}, {-1, 0},
  {-2,-1}, {-1,-1}, { 0,-1}, { 1,-1}, { 2,-1},
};
static constexpr int DP_ORBIT_LEN = (int)(sizeof(DP_ORBIT) / sizeof(DP_ORBIT[0]));

inline DpShift dpShiftForMinute(int minuteOfDay) {
  int i = minuteOfDay % DP_ORBIT_LEN;
  if (i < 0) i += DP_ORBIT_LEN;
  return DP_ORBIT[i];
}

// Shift an SSD1306-style framebuffer (page-organised, bit0 = top row of each page)
// by dx columns and dy rows (|dy| <= 1 per call is enough; larger values repeat).
// Pixels shifted out are lost; vacated pixels are cleared.
inline void dpShiftFrame(uint8_t* buf, int w, int pages, int dx, int dy) {
  if (!buf) return;
  for (int n = (dy < 0 ? -dy : dy); n > 0; n--) {
    for (int x = 0; x < w; x++) {
      if (dy > 0) {
        for (int p = pages - 1; p >= 0; p--) {
          const uint8_t above = p > 0 ? (uint8_t)(buf[(p - 1) * w + x] >> 7) : 0;
          buf[p * w + x] = (uint8_t)((buf[p * w + x] << 1) | above);
        }
      } else {
        for (int p = 0; p < pages; p++) {
          const uint8_t below = p < pages - 1 ? (uint8_t)(buf[(p + 1) * w + x] << 7) : 0;
          buf[p * w + x] = (uint8_t)((buf[p * w + x] >> 1) | below);
        }
      }
    }
  }
  if (dx == 0) return;
  for (int p = 0; p < pages; p++) {
    uint8_t* row = buf + p * w;
    if (dx > 0 && dx < w) { memmove(row + dx, row, (size_t)(w - dx)); memset(row, 0, (size_t)dx); }
    else if (dx < 0 && -dx < w) { memmove(row, row - dx, (size_t)(w + dx)); memset(row + w + dx, 0, (size_t)(-dx)); }
  }
}

// ---- Brightness ------------------------------------------------------------
enum DpBrightMode : uint8_t { DP_DIM = 0, DP_BRIGHT = 1, DP_AUTO_DIM = 2 };
static constexpr uint32_t DP_AUTO_DIM_MS = 30000;   // Auto Dim: dim after this long without interaction

// Contrast alone barely dims these panels, so Dim also lowers the pre-charge period and
// VCOMH level (tuned on the clock: contrast 0x40 + 0x11 + 0x00 looked right).
static constexpr uint8_t DP_CONTRAST_DIM      = 0x40;
static constexpr uint8_t DP_CONTRAST_BRIGHT   = 0x7F;   // moderate, not maximum
static constexpr uint8_t DP_PRECHARGE_DIM     = 0x11;
static constexpr uint8_t DP_PRECHARGE_BRIGHT  = 0xF1;   // SSD1306 default (internal charge pump)
static constexpr uint8_t DP_VCOMH_DIM         = 0x00;
static constexpr uint8_t DP_VCOMH_BRIGHT      = 0x40;   // SSD1306 default

// Dim: dim on the home screen, bright while the user interacts (or Adhan plays).
// Bright: always bright.
// Auto Dim: bright, then dim once idleMs (time since the last interaction) reaches 30 s;
//           any interaction or Adhan makes it bright again.
inline bool dpIsDim(uint8_t mode, bool interacting, uint32_t idleMs = 0) {
  if (interacting || mode == DP_BRIGHT) return false;
  if (mode == DP_AUTO_DIM) return idleMs >= DP_AUTO_DIM_MS;
  return true;
}
inline uint8_t dpContrast(uint8_t mode, bool interacting, uint32_t idleMs = 0) {
  return dpIsDim(mode, interacting, idleMs) ? DP_CONTRAST_DIM : DP_CONTRAST_BRIGHT;
}

// ---- Idle screen indicators ------------------------------------------------
// Volume dots only for a short window after a volume change (untilMs 0 = never).
inline bool dpShowVolumeDots(uint32_t now, uint32_t untilMs) {
  return untilMs != 0 && (int32_t)(untilMs - now) > 0;
}

// NO WIFI text: Wi-Fi was never set up, or the last connection attempt failed.
inline bool dpShowNoWifi(bool notSetUp, bool connectFailed) {
  return notSetUp || connectFailed;
}
