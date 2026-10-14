/*
 * PrayerCalc.cpp — Local Fajr/Asr calculation and best-fit search (AD-49).
 *
 * Follows the PrayTimes.org algorithm that the Aladhan API is built on (one
 * iteration from the default time guesses, nearest-minute rounding), plus the
 * Moonsighting Committee seasonal Fajr and Aladhan's per-method minute offsets.
 * Checked against saved Aladhan yearly responses by test/test_AD_49.
 * Pure C++ apart from the Arduino include, so the native test compiles it directly.
 */
#ifdef ARDUINO
#include <Arduino.h>
#endif
#include "PrayerCalc.h"
#include <math.h>

// ============================================================================
// Method table (const => stays in flash on the ESP32)
// ============================================================================
enum : uint8_t { PCF_NONE = 0, PCF_MOONSIGHTING = 1 };

struct PcMethod {
  int8_t  id;          // Aladhan method id
  float   fajrAngle;   // degrees below horizon
  int8_t  asrOffset;   // Aladhan tune offset for Asr, minutes
  uint8_t flags;
};

static const PcMethod kPcMethods[] = {
  {  0, 16.0f, 0, PCF_NONE },          // Jafari
  {  1, 18.0f, 0, PCF_NONE },          // Karachi
  {  2, 15.0f, 0, PCF_NONE },          // ISNA
  {  3, 18.0f, 0, PCF_NONE },          // MWL
  {  4, 18.5f, 0, PCF_NONE },          // Makkah (Umm al-Qura)
  {  5, 19.5f, 0, PCF_NONE },          // Egypt
  {  7, 17.7f, 0, PCF_NONE },          // Tehran
  {  8, 19.5f, 0, PCF_NONE },          // Gulf
  {  9, 18.0f, 0, PCF_NONE },          // Kuwait
  { 10, 18.0f, 0, PCF_NONE },          // Qatar
  { 11, 20.0f, 0, PCF_NONE },          // Singapore
  { 12, 12.0f, 0, PCF_NONE },          // France (UOIF)
  { 13, 18.0f, 4, PCF_NONE },          // Turkey (Diyanet): Asr +4
  { 14, 16.0f, 0, PCF_NONE },          // Russia
  { 15, 18.0f, 0, PCF_MOONSIGHTING },  // Moonsighting Committee
  { 16, 18.2f, 0, PCF_NONE },          // Dubai
  { 17, 20.0f, 0, PCF_NONE },          // Malaysia (JAKIM)
  { 18, 18.0f, 0, PCF_NONE },          // Tunisia
  { 19, 18.0f, 0, PCF_NONE },          // Algeria
  { 20, 20.0f, 0, PCF_NONE },          // Indonesia (KEMENAG)
  { 21, 19.0f, 0, PCF_NONE },          // Morocco
  { 22, 18.0f, 0, PCF_NONE },          // Lisbon
  { 23, 18.0f, 0, PCF_NONE },          // Jordan
};
static const int kPcMethodCount = sizeof(kPcMethods) / sizeof(kPcMethods[0]);

static const PcMethod* findMethod(int id) {
  for (int i = 0; i < kPcMethodCount; i++) {
    if (kPcMethods[i].id == id) return &kPcMethods[i];
  }
  return nullptr;
}

bool pcMethodSupported(int method) { return findMethod(method) != nullptr; }

// ============================================================================
// Astronomy helpers (degrees / hours, as in PrayTimes)
// ============================================================================
static const double kDeg = M_PI / 180.0;

static double dsin(double d) { return sin(d * kDeg); }
static double dcos(double d) { return cos(d * kDeg); }
static double dtan(double d) { return tan(d * kDeg); }
static double darcsin(double x) { return asin(x) / kDeg; }
static double darccos(double x) { return acos(x) / kDeg; }
static double darctan2(double y, double x) { return atan2(y, x) / kDeg; }
static double darccot(double x) { return atan(1.0 / x) / kDeg; }

static double fixRange(double a, double b) {
  a = a - b * floor(a / b);
  return (a < 0) ? a + b : a;
}
static double fixAngle(double a) { return fixRange(a, 360.0); }
static double fixHour(double a)  { return fixRange(a, 24.0); }

static double julian(int year, int month, int day) {
  if (month <= 2) { year -= 1; month += 12; }
  const double A = floor(year / 100.0);
  const double B = 2 - A + floor(A / 4.0);
  return floor(365.25 * (year + 4716)) + floor(30.6001 * (month + 1)) + day + B - 1524.5;
}

struct SunPos { double decl; double eqt; };

static SunPos sunPosition(double jd) {
  const double D = jd - 2451545.0;
  const double g = fixAngle(357.529 + 0.98560028 * D);
  const double q = fixAngle(280.459 + 0.98564736 * D);
  const double L = fixAngle(q + 1.915 * dsin(g) + 0.020 * dsin(2 * g));
  const double e = 23.439 - 0.00000036 * D;
  const double RA = darctan2(dcos(e) * dsin(L), dcos(L)) / 15.0;
  SunPos p;
  p.eqt  = q / 15.0 - fixHour(RA);
  p.decl = darcsin(dsin(e) * dsin(L));
  return p;
}

// Per-day context: sun position sampled at PrayTimes' default guesses
// (Fajr 5h, Sunrise 6h, Sunset 18h; one iteration), which reproduces Aladhan's
// Fajr and Sunrise exactly. Aladhan's Asr does not follow the 13h guess: sampling
// at 5.5h was the best fit across the test locations (all days within 1 minute).
struct DayCtx {
  double lat;
  double adjust;    // tz - lon/15, added to every raw time
  SunPos atFajr, atSunrise, atAsr, atSunset;
  double sunrise, sunset;   // local hours
};

static double sunAngleTime(const DayCtx& c, const SunPos& p, double angle, bool ccw);
static const double kRiseSetAngle = 0.833;

static void makeCtx(const PcDay& d, DayCtx& c) {
  const double jDate = julian(d.year, d.month, d.day) - d.lon / (15.0 * 24.0);
  c.lat = d.lat;
  c.adjust = d.tzHours - d.lon / 15.0;
  c.atFajr    = sunPosition(jDate + 5.0 / 24.0);
  c.atSunrise = sunPosition(jDate + 6.0 / 24.0);
  c.atAsr     = sunPosition(jDate + 5.5 / 24.0);
  c.atSunset  = sunPosition(jDate + 18.0 / 24.0);
  c.sunrise = sunAngleTime(c, c.atSunrise, kRiseSetAngle, true) + c.adjust;
  c.sunset  = sunAngleTime(c, c.atSunset,  kRiseSetAngle, false) + c.adjust;
}

// Time (hours, local mean) when the sun is `angle` below the horizon; NaN if never.
static double sunAngleTime(const DayCtx& c, const SunPos& p, double angle, bool ccw) {
  const double noon = fixHour(12.0 - p.eqt);
  const double x = (-dsin(angle) - dsin(p.decl) * dsin(c.lat)) / (dcos(p.decl) * dcos(c.lat));
  if (x < -1.0 || x > 1.0) return NAN;
  const double t = darccos(x) / 15.0;
  return noon + (ccw ? -t : t);
}


static int toMinutes(double h) {
  if (isnan(h)) return -1;
  // PrayTimes rounds to the nearest minute.
  const double r = fixHour(h + 0.5 / 60.0);
  return (int)floor(r * 60.0) % 1440;
}

// Moonsighting Committee seasonal Fajr: minutes before sunrise (Khalid Shaukat).
static double moonsightingFajrMinutes(const PcDay& d) {
  const double lat = fabs(d.lat);
  const double a = 75.0 + 28.65 / 55.0 * lat;
  const double b = 75.0 + 19.44 / 55.0 * lat;
  const double c = 75.0 + 32.74 / 55.0 * lat;
  const double e = 75.0 + 48.10 / 55.0 * lat;

  // Day of year, then days since the winter solstice of that hemisphere.
  static const int kCum[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
  const bool leap = (d.year % 4 == 0 && d.year % 100 != 0) || (d.year % 400 == 0);
  const int daysInYear = leap ? 366 : 365;
  int doy = kCum[d.month - 1] + d.day + ((leap && d.month > 2) ? 1 : 0);
  int dyy = doy + ((d.lat >= 0) ? 10 : (leap ? 173 : 172));
  if (dyy >= daysInYear) dyy -= daysInYear;

  if (dyy < 91)  return a + (b - a) / 91.0 * dyy;
  if (dyy < 137) return b + (c - b) / 46.0 * (dyy - 91);
  if (dyy < 183) return c + (e - c) / 46.0 * (dyy - 137);
  if (dyy < 229) return e + (c - e) / 46.0 * (dyy - 183);
  if (dyy < 275) return c + (b - c) / 46.0 * (dyy - 229);
  return b + (a - b) / 91.0 * (dyy - 275);
}

// Fajr in local hours for an already prepared day.
static double fajrH(const PcDay& d, const DayCtx& c, const PcMethod& m, int latAdj) {
  double fajr = sunAngleTime(c, c.atFajr, m.fajrAngle, true) + c.adjust;
  const double sunrise = c.sunrise;

  if (m.flags & PCF_MOONSIGHTING) {
    // Aladhan uses the seasonal time on its own (not the later of it and 18 degrees),
    // whatever the latitude rule.
    return sunrise - round(moonsightingFajrMinutes(d)) / 60.0;   // whole minutes, as Aladhan
  }

  if (latAdj > 0) {
    const double night = fixHour(sunrise - c.sunset);
    double portion;
    if (latAdj == 1)      portion = 0.5;                  // Middle of the night
    else if (latAdj == 2) portion = 1.0 / 7.0;            // One seventh
    else                  portion = m.fajrAngle / 60.0;   // Angle based
    portion *= night;
    const double diff = fixHour(sunrise - fajr);
    if (isnan(fajr) || diff > portion) fajr = sunrise - portion;
  } else if (isnan(fajr)) {
    // No rule and no twilight (high-latitude summer): Aladhan reports the middle of the night.
    fajr = sunrise - fixHour(sunrise - c.sunset) / 2.0;
  }
  return fajr;
}

static double asrH(const DayCtx& c, const PcMethod& m, int school) {
  const double factor = (school == 1) ? 2.0 : 1.0;
  const double angle = -darccot(factor + dtan(fabs(c.lat - c.atAsr.decl)));
  return sunAngleTime(c, c.atAsr, angle, false) + c.adjust + m.asrOffset / 60.0;
}

// ============================================================================
// Public API
// ============================================================================
int pcFajrMinutes(const PcDay& d, int method, int latAdj) {
  const PcMethod* m = findMethod(method);
  if (!m) return -1;
  DayCtx c; makeCtx(d, c);
  return toMinutes(fajrH(d, c, *m, latAdj));
}

int pcAsrMinutes(const PcDay& d, int method, int school) {
  const PcMethod* m = findMethod(method);
  if (!m) return -1;
  DayCtx c; makeCtx(d, c);
  return toMinutes(asrH(c, *m, school));
}

// Signed difference a - b in minutes, wrapped to -720..719.
static int diffMin(int a, int b) {
  int d = (a - b) % 1440;
  if (d < -720) d += 1440;
  if (d >= 720) d -= 1440;
  return d;
}

// Tie-break rank: lower is preferred.
static long tieRank(int method, int latAdj, int school, int curMethod, int curLatAdj, int curSchool) {
  long r = 0;
  r = r * 2 + (method == curMethod ? 0 : 1);
  r = r * 2 + (latAdj == curLatAdj ? 0 : 1);
  r = r * 2 + (school == curSchool ? 0 : 1);
  r = r * 32 + method;
  r = r * 4 + latAdj;
  r = r * 2 + school;
  return r;
}

bool pcBestFit(const PcDay& d, int mosqueFajr, int mosqueAsr,
               int curMethod, int curSchool, int curLatAdj, PcFit& out) {
  out = PcFit{};
  const bool haveFajr = mosqueFajr >= 0;
  const bool haveAsr  = mosqueAsr >= 0;
  if (!haveFajr && !haveAsr) return false;

  DayCtx c; makeCtx(d, c);
  long bestRank = 0;

  for (int i = 0; i < kPcMethodCount; i++) {
    const PcMethod& m = kPcMethods[i];
    // Asr only: method and latitude rule stay as saved.
    if (!haveFajr && m.id != curMethod) continue;

    for (int la = 0; la <= 3; la++) {
      if (!haveFajr && la != curLatAdj) continue;
      int fajr = -1;
      if (haveFajr) {
        fajr = toMinutes(fajrH(d, c, m, la));
        if (fajr < 0) continue;
      }

      for (int s = 0; s <= 1; s++) {
        // Fajr only: school stays as saved.
        if (!haveAsr && s != curSchool) continue;
        int asr = -1;
        if (haveAsr) {
          asr = toMinutes(asrH(c, m, s));
          if (asr < 0) continue;
        }

        const int dF = haveFajr ? diffMin(fajr, mosqueFajr) : 0;
        const int dA = haveAsr ? diffMin(asr, mosqueAsr) : 0;
        const int score = (dF < 0 ? -dF : dF) + (dA < 0 ? -dA : dA);
        const long rank = tieRank(m.id, la, s, curMethod, curLatAdj, curSchool);

        if (!out.ok || score < out.score || (score == out.score && rank < bestRank)) {
          out.ok = true;
          out.method = m.id; out.school = s; out.latAdj = la;
          out.fajr = fajr; out.asr = asr;
          out.dFajr = dF; out.dAsr = dA; out.score = score;
          bestRank = rank;
        }
      }
    }
  }

  // Asr only with an unsupported saved method: fall back to evaluating it with MWL angles.
  if (!out.ok && !haveFajr) {
    for (int s = 0; s <= 1; s++) {
      const PcMethod* m = findMethod(3);
      const int asr = toMinutes(asrH(c, *m, s));
      const int dA = diffMin(asr, mosqueAsr);
      const int score = dA < 0 ? -dA : dA;
      if (asr >= 0 && (!out.ok || score < out.score)) {
        out.ok = true; out.method = curMethod; out.school = s; out.latAdj = curLatAdj;
        out.asr = asr; out.dAsr = dA; out.score = score;
      }
    }
  }
  return out.ok;
}
