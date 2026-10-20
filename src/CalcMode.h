/*
 * CalcMode.h — prayer calculation mode: Auto or Manual (AD-51).
 *
 * Header-only and plain C++ so test_AD_51 can check it natively.
 *   Auto   : Aladhan picks the method and high-latitude rule; only the Asr school is sent.
 *   Manual : method, Asr school and high-latitude rule are sent as chosen (or fitted from
 *            the mosque's Fajr/Asr by the AD-49 smart fit).
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum CalcMode : uint8_t { CALC_AUTO = 0, CALC_MANUAL = 1 };

// Mode for a device loading its settings. A stored mode wins. Otherwise a device that
// already saved method/school/latitude keys keeps its times by moving to Manual; a new
// or factory-reset device starts in Auto. Must run before anything saves settings.
inline uint8_t cmResolveMode(bool haveStoredMode, uint8_t stored, bool haveLegacyKeys) {
  if (haveStoredMode) return stored == CALC_MANUAL ? CALC_MANUAL : CALC_AUTO;
  return haveLegacyKeys ? CALC_MANUAL : CALC_AUTO;
}

// Query-string tail for the Aladhan timings request (starts with '&').
// Auto omits method and latitudeAdjustmentMethod so Aladhan chooses them.
inline void cmTimingsParams(char* buf, size_t n, uint8_t mode, int method, int school, int latAdj) {
  if (mode == CALC_MANUAL)
    snprintf(buf, n, "&method=%d&school=%d&latitudeAdjustmentMethod=%d", method, school, latAdj);
  else
    snprintf(buf, n, "&school=%d", school);
}

// Asr Time labels. school: 1 = Mithl 1 (Hanafi), 0 = Mithl 2 (Shafi'i, Maliki, Hanbali).
inline const char* cmAsrLabelDevice(int school) {
  return school == 1 ? "Mithl 1 (Hanafi)" : "Mithl 2 (Others)";
}
inline const char* cmAsrLabelPortal(int school) {
  return school == 1 ? "Mithl 1: Hanafi" : "Mithl 2: Shafi'i, Maliki & Hanbali";
}
