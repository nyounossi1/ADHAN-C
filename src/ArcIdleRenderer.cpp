#include "ArcIdleRenderer.h"
#include <math.h>

ArcIdleRenderer::ArcIdleRenderer() {}

void ArcIdleRenderer::begin(Adafruit_SSD1306* d) {
  display = d;
}

void ArcIdleRenderer::setPrayerTimes(const ArcPrayerTimes& p) {
  pt = p;
  recomputeCurveRange();
}

void ArcIdleRenderer::setPrefs(const ArcUserPrefs& p) {
  prefs = p;
  recomputeCurveRange();
}

int ArcIdleRenderer::clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

int ArcIdleRenderer::toDayMin(int m) {
  m %= (24 * 60);
  if (m < 0) m += (24 * 60);
  return m;
}

void ArcIdleRenderer::fmtTime(char out[8], int nowMin, bool is24h) {
  int m = toDayMin(nowMin);
  int h = (m / 60) % 24;
  int mi = m % 60;

  if (is24h) {
    snprintf(out, 6, "%02d:%02d", h, mi);
  } else {
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    // "7:05" + space for AM/PM if you want later; keeping compact:
    snprintf(out, 6, "%d:%02d", h12, mi);
  }
}

void ArcIdleRenderer::recomputeCurveRange() {
  // If prayer times not set yet, keep something sane
  if (pt.fajr <= 0 && pt.isha <= 0) {
    tCurveStart = 0;
    tCurveEnd   = 1;
    bellYCacheValid = false;
    return;
  }
  tCurveStart = pt.fajr - prefs.curvePadMin;
  tCurveEnd   = pt.isha + prefs.curvePadMin;

  // Pre-compute bell curve Y for every X pixel (eliminates expf/powf per frame)
  for (int x = X_CURVE0; x <= X_CURVE1; x++) {
    bellYCache[x - X_CURVE0] = (int8_t)computeBellY(x);
  }
  bellYCacheValid = true;
}

int ArcIdleRenderer::timeToX(int tMin) const {
  if (tMin <= tCurveStart) return X_CURVE0;
  if (tMin >= tCurveEnd)   return X_CURVE1;

  const float span = float(tCurveEnd - tCurveStart);
  const float u = float(tMin - tCurveStart) / span;
  int x = X_CURVE0 + (int)lroundf(u * float(X_CURVE1 - X_CURVE0));
  return clampi(x, X_CURVE0, X_CURVE1);
}

int ArcIdleRenderer::computeBellY(int x) const {
  const int x0 = X_CURVE0, x1 = X_CURVE1, yBase = Y_LINE;
  if (x1 <= x0) return yBase;

  const float cx = 0.5f * (x0 + x1);
  const float halfW = 0.5f * (x1 - x0);

  const float sigma = halfW / 2.2f;
  const float inv2s2 = 1.0f / (2.0f * sigma * sigma);

  auto G = [&](float xf) -> float {
    float d = xf - cx;
    return expf(-(d * d) * inv2s2);
  };

  const float gEnd = G((float)x0);
  const float denom = 1.0f - gEnd;

  auto H = [&](float xf) -> float {
    float v = (G(xf) - gEnd) / denom;
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    return v;
  };

  const float p = 2.2f;
  float t = fabsf((float)x - cx) / halfW;
  if (t < 0) t = 0;
  if (t > 1) t = 1;

  float hump  = H((float)x);
  float droop = powf(t, p);

  int y = yBase
        - (int)lroundf(hump * PEAK_UP_PX)
        + (int)lroundf(droop * EDGE_DOWN_PX);

  return clampi(y, 0, 63);
}

int ArcIdleRenderer::bellYatX(int x) const {
  // Fast path: return from pre-computed LUT
  if (bellYCacheValid && x >= X_CURVE0 && x <= X_CURVE1) {
    return bellYCache[x - X_CURVE0];
  }
  // Fallback (should only happen before first setPrayerTimes)
  return computeBellY(x);
}

void ArcIdleRenderer::drawDottedHorizon() {
  int x = X_LINE0;
  while (x <= X_LINE1) {
    display->drawPixel(x, Y_LINE, SSD1306_WHITE);
    display->drawPixel(x + 1, Y_LINE, SSD1306_WHITE);
    x += 4;
  }
}

void ArcIdleRenderer::drawBellFaint() {
  for (int x = X_CURVE0; x <= X_CURVE1; x += 2) {
    int y = bellYatX(x);
    display->drawPixel(x, y, SSD1306_WHITE);
  }
}

void ArcIdleRenderer::drawBellProgress(int xEnd) {
  if (xEnd < X_CURVE0) return;
  if (xEnd > X_CURVE1) xEnd = X_CURVE1;

  int prevX = X_CURVE0;
  int prevY = bellYatX(prevX);

  for (int x = X_CURVE0 + 1; x <= xEnd; x++) {
    int y = bellYatX(x);
    display->drawLine(prevX, prevY, x, y, SSD1306_WHITE);
    prevX = x; prevY = y;
  }
}

int ArcIdleRenderer::findLeftCrossingX() const {
  const int cx = (X_CURVE0 + X_CURVE1) / 2;
  for (int x = X_CURVE0; x <= cx; x++) {
    int y = bellYatX(x);
    if (y <= Y_LINE) return x;
  }
  return cx;
}

int ArcIdleRenderer::findRightCrossingX() const {
  const int cx = (X_CURVE0 + X_CURVE1) / 2;
  int lastY = bellYatX(cx);

  for (int x = cx + 1; x <= X_CURVE1; x++) {
    int y = bellYatX(x);
    if (y == Y_LINE) return x;

    bool lastAbove = (lastY < Y_LINE);
    bool nowBelowOrOn = (y >= Y_LINE);
    if (lastAbove && nowBelowOrOn) {
      if (abs(lastY - Y_LINE) <= abs(y - Y_LINE)) return x - 1;
      return x;
    }
    lastY = y;
  }
  return X_CURVE1;
}

void ArcIdleRenderer::computeMarkers(Marker M[5]) const {
  const int cx = (X_CURVE0 + X_CURVE1) / 2;

  // #2 Peak
  M[1].x = cx;
  M[1].y = bellYatX(M[1].x);

  // #4 Right crossing
  M[3].x = findRightCrossingX();
  M[3].y = bellYatX(M[3].x);

  // #1 Left dip below horizon
  int m1x = X_CURVE0;
  for (int x = X_CURVE0; x < cx; x++) {
    int y = bellYatX(x);
    if (y >= Y_LINE + 3) { m1x = x; break; }
  }
  M[0].x = m1x;
  M[0].y = bellYatX(M[0].x);

  // #5 Mirror of #1
  M[4].x = cx + (cx - M[0].x);
  M[4].y = bellYatX(M[4].x);

  // #3 (ASR) position between Dhuhr(peak) and Maghrib(horizon crossing),
  // based on where Asr time falls between Dhuhr and Maghrib.
  int magX = M[3].x; // right crossing already computed above

  int dhuhrT   = toDayMin(pt.dhuhr);
  int asrT     = toDayMin(pt.asr);
  int maghribT = toDayMin(pt.maghrib);

  // Guard: if times are weird or identical, fall back to the old "looks good" placement.
  int xAsr;
  if (maghribT <= dhuhrT) {
    xAsr = cx + (int)lroundf(0.70f * float(magX - cx));
  } else {
    float u = float(asrT - dhuhrT) / float(maghribT - dhuhrT); // 0..1
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;

    // Optional: a tiny easing so it doesn't hug ends too aggressively:
    // u = u*u*(3.0f - 2.0f*u);   // smoothstep, comment out if you want strictly linear

    xAsr = cx + (int)lroundf(u * float(magX - cx));
  }

  // Keep it drawable and not colliding with neighbors
  xAsr = clampi(xAsr, cx + 1, magX - 1);

  M[2].x = xAsr;
  M[2].y = bellYatX(M[2].x);
}

bool ArcIdleRenderer::markerFilled(int markerX, int xProgressEnd) {
  // At Isha time, xProgressEnd should be X_CURVE1
  // All markers should be filled when we're at or past them
  return xProgressEnd >= markerX;
}

const char* ArcIdleRenderer::currentLabel(int nowMin, int /*xEnd*/, int /*sunriseX*/, const Marker /*M*/[5]) const {
  const int tod = toDayMin(nowMin);

  // Fallback sunrise if not provided
  const int sunriseMin = (pt.sunrise >= 0) ? toDayMin(pt.sunrise) : toDayMin(pt.fajr + 90);

  const int fajr    = toDayMin(pt.fajr);
  const int dhuhr   = toDayMin(pt.dhuhr);
  const int asr     = toDayMin(pt.asr);
  const int mag     = toDayMin(pt.maghrib);
  const int isha    = toDayMin(pt.isha);

  // Night / Isha split across midnight:
  // Isha start -> 00:00 is "ISHA"
  if (tod >= isha) return "ISHA";

  // 00:00 -> Fajr is "NIGHT"
  if (tod < fajr) return "NIGHT";

  // Daytime labels
  if (tod < sunriseMin) return "FAJR";
  if (tod < dhuhr)      return "DAY";
  if (tod < asr)        return "DHUHR";
  if (tod < mag)        return "ASR";
  if (tod < isha)       return "MAGHRIB";

  // Fallback (should be unreachable because tod>=isha handled above)
  return "ISHA";
}

void ArcIdleRenderer::drawCenteredTextClampedY(const char* txt, int yPreferred, uint8_t size) {
  display->setFont(nullptr);
  display->setTextSize(size);

  int16_t x1, y1;
  uint16_t w, h;
  display->getTextBounds(txt, 0, 0, &x1, &y1, &w, &h);

  int x = (128 - (int)w) / 2;
  if (x < 0) x = 0;

  int yMax = 64 - (int)h;
  int y = clampi(yPreferred, 0, yMax);

  display->setCursor(x, y);
  display->print(txt);
}

void ArcIdleRenderer::drawSun(int cx, int cy, int r) {
  display->fillCircle(cx, cy, r, SSD1306_WHITE);

  const int inner = r + 1;
  const int outer = r + 4;

  auto ray = [&](int dx, int dy) {
    display->drawLine(cx + dx * inner, cy + dy * inner,
                      cx + dx * outer, cy + dy * outer,
                      SSD1306_WHITE);
  };

  ray( 1,  0); ray(-1,  0); ray( 0,  1); ray( 0, -1);
  ray( 1,  1); ray(-1, -1); ray( 1, -1); ray(-1,  1);
}

void ArcIdleRenderer::drawMoon(int cx, int cy, int r) {
  display->fillCircle(cx, cy, r, SSD1306_WHITE);
}

void ArcIdleRenderer::render(int nowMin) {
  if (!display) return;

  // Clear the display first
  display->clearDisplay();
  display->setTextColor(SSD1306_WHITE);

  // --- Compute xEnd (progress end X) ---
  // We want the arc to be "complete" from Isha onward (including night before Fajr),
  // even if the curve geometry includes padding beyond Isha for nicer spacing.
  const int tod = toDayMin(nowMin);
  const bool isNightComplete = (tod >= pt.isha) || (tod < pt.fajr);

  // Map time->X so that Isha lands exactly on X_CURVE1.
  // (Using tCurveStart as the left bound, but pt.isha as the progress end.)
  auto timeToXProgress = [&](int tMin) -> int {
    if (tMin <= tCurveStart) return X_CURVE0 - 1;

    const int tEndProg = pt.isha;               // progress ends at Isha
    const int span = (tEndProg - tCurveStart);
    if (span <= 0) return timeToX(tMin);        // safety fallback

    if (tMin >= tEndProg) return X_CURVE1;

    const float u = float(tMin - tCurveStart) / float(span);
    int x = X_CURVE0 + (int)lroundf(u * float(X_CURVE1 - X_CURVE0));
    return clampi(x, X_CURVE0, X_CURVE1);
  };

  int xEnd;
  if (isNightComplete) {
    xEnd = X_CURVE1;
  } else {
    xEnd = timeToXProgress(tod);
  }

  // --- Markers / layout helpers ---
  Marker M[5];
  computeMarkers(M);
  const int sunriseX = findLeftCrossingX();  // Used in currentLabel in working version

  // Horizon + curves - DRAW THESE FIRST
  drawDottedHorizon();

  // ALWAYS draw the faint bell curve (like in working version)
  drawBellFaint();

  // Then draw the progress on top
  drawBellProgress(xEnd);

  // 5 markers
  for (int i = 0; i < 5; i++) {
    bool filled = (xEnd >= M[i].x);
    if (filled) {
      display->fillCircle(M[i].x, M[i].y, MARK_R, SSD1306_WHITE);
    } else {
      display->drawCircle(M[i].x, M[i].y, MARK_R, SSD1306_WHITE);
    }
  }

  // Sun/Moon marker ONLY between fajr and isha (time-of-day)
  const bool showCelestial = (tod >= toDayMin(pt.fajr) && tod < toDayMin(pt.isha));

  if (showCelestial && xEnd >= X_CURVE0) {
    int yEnd = bellYatX(xEnd);
    if (yEnd > Y_LINE) {
      drawMoon(xEnd, yEnd, PROG_R);
    } else {
      drawSun(xEnd, yEnd, PROG_R);
    }
  }

  // NOW draw text LAST (so it's on top)
  char timeStr[8];
  fmtTime(timeStr, nowMin, prefs.is24h);

  // Use the original currentLabel function with all parameters
  const char* prayerLabel = currentLabel(nowMin, xEnd, sunriseX, M);

  // Use the working version's text positioning
  const int yTimePreferred   = Y_LINE - 12;
  const int yPrayerPreferred = M[0].y - 6;

  drawCenteredTextClampedY(timeStr, yTimePreferred, 1);
  drawCenteredTextClampedY(prayerLabel, yPrayerPreferred, 1);

  // Display everything
  //display->display();
}

// (end of file)