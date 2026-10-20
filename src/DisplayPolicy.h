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
enum DpBrightMode : uint8_t { DP_DIM = 0, DP_BRIGHT = 1 };

static constexpr uint8_t DP_CONTRAST_DIM    = 0x1F;
static constexpr uint8_t DP_CONTRAST_BRIGHT = 0x7F;   // moderate, not maximum

// Dim: low contrast on the home screen, bright while the user interacts.
// Bright: always bright.
inline uint8_t dpContrast(uint8_t mode, bool interacting) {
  return (mode == DP_BRIGHT || interacting) ? DP_CONTRAST_BRIGHT : DP_CONTRAST_DIM;
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
