/*
 * SmartCalcEntry.h — HH:MM entry steps for the Smart calculation screen (AD-49).
 *
 * Header-only and plain C++ so test_AD_49 can check it natively.
 * A time is minutes since midnight (0..1439) or -1 for "skipped" (--:--).
 */
#pragma once

// Fields edited in order: Fajr hour, Fajr minute, Asr hour, Asr minute, then the result.
enum SmartCalcStep : int { SC_FAJR_H = 0, SC_FAJR_M, SC_ASR_H, SC_ASR_M, SC_RESULT };

// Hour field: a single press steps through --, 00..23 and wraps. Leaving "--"
// restores the minutes of `seed`; entering "--" skips this prayer. Auto-repeat
// (allowSkip = false) wraps 23 <-> 00 and never lands on "--".
inline int scStepHour(int t, int dir, int seed, bool allowSkip = true) {
  int h = (t < 0) ? -1 : t / 60;
  const int m = (t < 0) ? ((seed < 0) ? 0 : seed % 60) : t % 60;
  h += (dir > 0) ? 1 : -1;
  if (allowSkip) {
    if (h > 23) h = -1;
    if (h < -1) h = 23;
  } else {
    if (h > 23) h = 0;
    if (h < 0)  h = 23;
  }
  return (h < 0) ? -1 : h * 60 + m;
}

// Auto-repeat timing while UP/DN is held (input task).
static const unsigned SC_REPEAT_DELAY_MS = 450;   // hold this long before repeating
static const unsigned SC_REPEAT_SLOW_MS  = 120;   // repeat interval for the first second
static const unsigned SC_REPEAT_FAST_MS  = 60;    // then faster
static const unsigned SC_REPEAT_FAST_AFTER_MS = 1450;

// Interval before the next repeat for a button held `heldMs` so far.
inline unsigned scRepeatIntervalMs(unsigned heldMs) {
  return (heldMs >= SC_REPEAT_FAST_AFTER_MS) ? SC_REPEAT_FAST_MS : SC_REPEAT_SLOW_MS;
}

// Minute field: UP/DN steps 00..59 and wraps within the same hour.
inline int scStepMinute(int t, int dir) {
  if (t < 0) return t;
  const int h = t / 60;
  int m = t % 60 + ((dir > 0) ? 1 : -1);
  if (m > 59) m = 0;
  if (m < 0) m = 59;
  return h * 60 + m;
}

// Field after OK: a skipped prayer jumps over its minute field.
inline int scNextStep(int step, int fajr, int asr) {
  if (step == SC_FAJR_H) return (fajr < 0) ? SC_ASR_H : SC_FAJR_M;
  if (step == SC_FAJR_M) return SC_ASR_H;
  if (step == SC_ASR_H)  return (asr < 0) ? SC_RESULT : SC_ASR_M;
  return SC_RESULT;
}
