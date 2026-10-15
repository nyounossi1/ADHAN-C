/*
 * LocationQuery.h — parsing for the manual location entry (AD-50).
 *
 * Header-only and plain C++ so test_AD_50 can check it natively. Decides how a
 * typed location is resolved: coordinates are used as typed, UK postcodes go to
 * postcodes.io, anything else is a town name for Open-Meteo geocoding.
 */
#pragma once
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum LocQueryKind : int { LQ_EMPTY = 0, LQ_LATLON, LQ_POSTCODE, LQ_TOWN };

// "51.2625, -1.0871" or "51.2625 -1.0871" -> true with lat/lon in range.
inline bool lqParseLatLon(const char* s, double& lat, double& lon) {
  if (!s) return false;
  char* end = nullptr;
  const double a = strtod(s, &end);
  if (end == s) return false;
  const char* p = end;
  while (*p == ' ' || *p == '\t') p++;
  if (*p == ',') p++;
  const char* q = p;
  const double b = strtod(q, &end);
  if (end == q) return false;
  while (*end == ' ' || *end == '\t') end++;
  if (*end != '\0') return false;
  if (a < -90.0 || a > 90.0 || b < -180.0 || b > 180.0) return false;
  lat = a; lon = b;
  return true;
}

// Uppercase, spaces removed. Returns false if it does not fit in `out`.
inline bool lqCompactUpper(const char* s, char* out, size_t n) {
  size_t k = 0;
  for (; s && *s; s++) {
    if (*s == ' ') continue;
    if (k + 1 >= n) return false;
    out[k++] = (char)toupper((unsigned char)*s);
  }
  if (n) out[k] = '\0';
  return true;
}

// Outward code, e.g. "RG21", "W1A", "SW1A", "M1": A[A]9[A9]. Returns its length or 0.
inline int lqOutcodeLen(const char* s) {
  int i = 0;
  if (!isalpha((unsigned char)s[i])) return 0;
  i++;
  if (isalpha((unsigned char)s[i])) i++;
  if (!isdigit((unsigned char)s[i])) return 0;
  i++;
  if (isalnum((unsigned char)s[i])) {
    // A trailing digit+letter+letter is the inward code, not part of the outcode.
    const bool inwardFollows = isdigit((unsigned char)s[i]) && isalpha((unsigned char)s[i + 1]) &&
                               isalpha((unsigned char)s[i + 2]) && s[i + 3] == '\0';
    if (!inwardFollows) i++;
  }
  return i;
}

// UK postcode or outcode ("RG21", "RG21 4AA", "rg214aa"). `full` = has the inward part.
// `compact` receives the uppercase form without spaces.
inline bool lqIsUkPostcode(const char* s, bool& full, char* compact, size_t n) {
  if (!lqCompactUpper(s, compact, n) || !compact[0]) return false;
  const int o = lqOutcodeLen(compact);
  if (o == 0) return false;
  const char* r = compact + o;
  if (*r == '\0') { full = false; return true; }
  if (isdigit((unsigned char)r[0]) && isalpha((unsigned char)r[1]) && isalpha((unsigned char)r[2]) && r[3] == '\0') {
    full = true; return true;
  }
  return false;
}

inline LocQueryKind lqClassify(const char* s) {
  while (s && (*s == ' ' || *s == '\t')) s++;
  if (!s || !*s) return LQ_EMPTY;
  double a, b;
  if (lqParseLatLon(s, a, b)) return LQ_LATLON;
  bool full; char c[16];
  if (lqIsUkPostcode(s, full, c, sizeof(c))) return LQ_POSTCODE;
  return LQ_TOWN;
}

// RFC 3986 percent-encoding. Returns false if it does not fit.
inline bool lqUrlEncode(const char* s, char* out, size_t n) {
  static const char* hex = "0123456789ABCDEF";
  size_t k = 0;
  for (; s && *s; s++) {
    const unsigned char c = (unsigned char)*s;
    const bool ok = isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~';
    if (k + (ok ? 1 : 3) >= n) return false;
    if (ok) out[k++] = (char)c;
    else { out[k++] = '%'; out[k++] = hex[c >> 4]; out[k++] = hex[c & 15]; }
  }
  if (n) out[k] = '\0';
  return true;
}

// Ambiguous town names: first result in the preferred country, else the first result.
inline int lqPickResult(const char* const* countryCodes, int count, const char* preferCc) {
  if (count <= 0) return -1;
  if (preferCc && preferCc[0]) {
    for (int i = 0; i < count; i++) {
      if (countryCodes[i] && strcasecmp(countryCodes[i], preferCc) == 0) return i;
    }
  }
  return 0;
}

// Location mode stored in NVS (locMode).
enum LocMode : int { LOC_AUTO = 0, LOC_MANUAL = 1 };

// Manual coordinates win only when manual mode has resolved coordinates.
inline bool lqUseManual(int mode, bool haveManualCoords) {
  return mode == LOC_MANUAL && haveManualCoords;
}
