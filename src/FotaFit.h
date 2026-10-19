/*
 * FotaFit.h — does an OTA image fit the device's update slot? (AD-53)
 *
 * Header-only and plain C++ so test_AD_53 can check it natively. Devices flashed
 * with the Arduino default partition table have 1,310,720 B OTA slots and never
 * gain the larger min_spiffs slots through OTA, so an image above that size can
 * only fail on them.
 */
#pragma once
#include <stddef.h>

// manifestSize is the "size" field of latest.json (<= 0 when missing or invalid).
// A missing size is treated as "fits" so older manifests behave as before.
inline bool fotaFitsSlot(long manifestSize, size_t slotSize) {
  return manifestSize <= 0 || (size_t)manifestSize <= slotSize;
}
