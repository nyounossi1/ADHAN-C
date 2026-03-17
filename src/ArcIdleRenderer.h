#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

struct ArcPrayerTimes {
  // minutes from midnight (0..1439) for *today*
  int fajr;
  int sunrise;   // if you don't have sunrise, set to -1 and we’ll approximate horizon-crossing
  int dhuhr;
  int asr;
  int maghrib;
  int isha;
};

struct ArcUserPrefs {
  bool is24h = true;     // user setting
  int  curvePadMin = 30; // start/end padding relative to Fajr/Isha
};

class ArcIdleRenderer {
public:
  ArcIdleRenderer();

  void begin(Adafruit_SSD1306* d);

  // Call whenever prayer times change (new day, location/method change, API refresh)
  void setPrayerTimes(const ArcPrayerTimes& pt);

  // Call whenever user prefs change
  void setPrefs(const ArcUserPrefs& prefs);

  // Render: draws ONLY the arc/bell-curve scene (no clearDisplay(), no display(), no overlays).
  // The UI compositor is responsible for clearing, overlays (time/volume/banner), and display().
  // nowMin = minutes from midnight for local time
  void render(int nowMin);

private:
  Adafruit_SSD1306* display = nullptr;
  ArcPrayerTimes pt{};
  ArcUserPrefs prefs{};

  // geometry
  static constexpr int Y_LINE   = 35; //45
  static constexpr int X_LINE0  = 2;
  static constexpr int X_LINE1  = 126;
  static constexpr int X_CURVE0 = 10;
  static constexpr int X_CURVE1 = 118;

  static constexpr int PEAK_UP_PX   = 30; //40
  static constexpr int EDGE_DOWN_PX = 12; //15

  static constexpr int MARK_R = 3;  // 6px dia
  static constexpr int PROG_R = 4;  // 8px dia

  struct Marker { int x; int y; };

  // derived curve range
  int tCurveStart = 0;
  int tCurveEnd   = 0;

  // Pre-computed bell curve Y values (avoids expf/powf per frame)
  static constexpr int CURVE_POINTS = X_CURVE1 - X_CURVE0 + 1;
  int8_t bellYCache[CURVE_POINTS] = {};
  bool bellYCacheValid = false;

  // helpers
  static int clampi(int v, int lo, int hi);
  static int toDayMin(int m); // normalize to 0..1439
  static void fmtTime(char out[8], int nowMin, bool is24h);

  int timeToX(int tMin) const;
  int bellYatX(int x) const;
  int computeBellY(int x) const;  // expensive math — called only when prayer times change

  void drawDottedHorizon();
  void drawBellFaint();
  void drawBellProgress(int xEnd);

  void computeMarkers(Marker M[5]) const;
  int findLeftCrossingX() const;
  int findRightCrossingX() const;

  static bool markerFilled(int markerX, int xProgressEnd);

  const char* currentLabel(int nowMin, int xEnd, int sunriseX, const Marker M[5]) const;

  void drawCenteredTextClampedY(const char* txt, int yPreferred, uint8_t size);

  void drawSun(int cx, int cy, int r);
  void drawMoon(int cx, int cy, int r);

  void recomputeCurveRange();
};
