/*
 * PrayerCalc.h — Local (offline) Fajr/Asr calculation and best-fit search (AD-49).
 *
 * Reproduces the Aladhan API's Fajr and Asr for a method / school / latitude rule,
 * so the device can pick the settings that match a mosque's times without any
 * network calls. Pure C++ (no Arduino headers) so it also builds in the host test.
 */
#pragma once
#include <stdint.h>

// Inputs for one day at one location.
struct PcDay {
  double lat;        // degrees, north positive
  double lon;        // degrees, east positive
  int    year;       // local calendar date
  int    month;      // 1..12
  int    day;        // 1..31
  double tzHours;    // UTC offset of that date in hours (e.g. 1.0 for BST)
};

// Result of the best-fit search. Minutes are minutes since local midnight.
struct PcFit {
  bool ok        = false;  // a candidate was found
  int  method    = -1;     // Aladhan method id
  int  school    = -1;     // 0 Shafi, 1 Hanafi
  int  latAdj    = -1;     // 0 None, 1 Middle of Night, 2 1/7 Night, 3 Angle Based
  int  fajr      = -1;     // computed Fajr for the chosen settings (-1 if not computed)
  int  asr       = -1;     // computed Asr for the chosen settings (-1 if not computed)
  int  dFajr     = 0;      // computed - mosque, minutes (0 if Fajr not entered)
  int  dAsr      = 0;      // computed - mosque, minutes (0 if Asr not entered)
  int  score     = 0;      // |dFajr| + |dAsr|
};

// Best-fit tolerance: above this the match is reported as "no close match".
static const int PC_CLOSE_MATCH_MAX_MIN = 10;

// True if the Aladhan method id exists (0..23 except 6).
bool pcMethodSupported(int method);

// Single-prayer computations. Return -1 if the time does not exist for that day
// (e.g. no Fajr twilight at high latitude with latAdj 0).
int pcFajrMinutes(const PcDay& d, int method, int latAdj);
int pcAsrMinutes(const PcDay& d, int method, int school);

// Search every supported method x latitude rule x school for the settings whose
// Fajr/Asr best match the mosque's times (minutes since midnight, -1 = not entered).
//  - Both given: score = |dFajr| + |dAsr| over all combinations.
//  - Fajr only:  picks method + latitude rule, school stays curSchool.
//  - Asr only:   picks school, method + latitude rule stay as current.
// Ties: prefer the current method, then the current latitude rule, then the current
// school, then the lowest method id (then lowest rule / school).
// Returns false if neither time was entered.
bool pcBestFit(const PcDay& d, int mosqueFajr, int mosqueAsr,
               int curMethod, int curSchool, int curLatAdj, PcFit& out);
