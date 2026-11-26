#pragma once
// Zeichenschicht des Monitors. Bewusst ohne Arduino-Abhängigkeit, damit
// dieselben Seiten am PC als Vorschaubild gerendert werden können
// (tools/vorschau.cpp) - was dort richtig aussieht, sieht auf dem Board gleich aus.
//
// Koordinaten sind immer die des Bildes, wie man es an der Wand sieht
// (480 x 480, oben links 0/0). Im Speicher liegt die Leinwand aber schon in der
// Lage des Panels: Der Halter dreht das Board um 90 Grad, und so wird das
// Übertragen in den Bildspeicher ein reines Zeilenkopieren ohne Umrechnen.
//   Panel (px, py) = (y, 479 - x)
//
// Kantenglättung: Jede Form wird über ihren Abstand zur Kante gezeichnet. Ein
// Pixel, dessen Mitte genau auf der Kante liegt, bekommt halbe Deckung - so sind
// Kreise, Bögen und schräge Linien weich wie in der App.

#include <stdint.h>
#include <string.h>
#include <math.h>
#include "schrift.h"

static inline uint16_t rgb(uint32_t c) {
  return (uint16_t)((((c >> 16) & 0xF8) << 8) | (((c >> 8) & 0xFC) << 3) | ((c & 0xFF) >> 3));
}
static inline float klemm(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

struct Leinwand {
  uint16_t *buf = nullptr;
  int ox = 0, oy = 0, w = 480, h = 480;        // Ausschnitt des Bildes, den diese Leinwand trägt
  int kx0 = 0, ky0 = 0, kx1 = 480, ky1 = 480;  // Beschnitt (Bildkoordinaten, Ende exklusiv)

  void init(uint16_t *b, int x, int y, int bw, int bh) {
    buf = b; ox = x; oy = y; w = bw; h = bh; volleFlaeche();
  }
  void volleFlaeche() { kx0 = ox; ky0 = oy; kx1 = ox + w; ky1 = oy + h; }
  void beschnitt(int x0, int y0, int x1, int y1) {
    kx0 = x0 > ox ? x0 : ox; ky0 = y0 > oy ? y0 : oy;
    kx1 = x1 < ox + w ? x1 : ox + w; ky1 = y1 < oy + h ? y1 : oy + h;
  }
  inline uint16_t *an(int x, int y) { return buf + (ox + w - 1 - x) * h + (y - oy); }
  inline bool drin(int x, int y) const { return x >= kx0 && x < kx1 && y >= ky0 && y < ky1; }

  // Mischt Farbe c mit Deckung a (0..256) in das Pixel.
  inline void misch(int x, int y, uint16_t c, int a) {
    if (a <= 0 || !drin(x, y)) return;
    uint16_t *p = an(x, y);
    if (a >= 256) { *p = c; return; }
    uint16_t z = *p;
    int r = ((z >> 11) * (256 - a) + (c >> 11) * a) >> 8;
    int g = (((z >> 5) & 63) * (256 - a) + ((c >> 5) & 63) * a) >> 8;
    int b = ((z & 31) * (256 - a) + (c & 31) * a) >> 8;
    *p = (uint16_t)((r << 11) | (g << 5) | b);
  }
  // Deckung aus vorzeichenbehaftetem Abstand (negativ = innen).
  inline void abstand(int x, int y, float d, uint16_t c) {
    if (d >= 0.5f) return;
    misch(x, y, c, d <= -0.5f ? 256 : (int)((0.5f - d) * 256));
  }

  void flaeche(int x, int y, int fw, int fh, uint16_t c) {
    int x0 = x > kx0 ? x : kx0, x1 = x + fw < kx1 ? x + fw : kx1;
    int y0 = y > ky0 ? y : ky0, y1 = y + fh < ky1 ? y + fh : ky1;
    if (x0 >= x1 || y0 >= y1) return;
    for (int xx = x0; xx < x1; xx++) {
      uint16_t *p = an(xx, y0);
      for (int yy = y0; yy < y1; yy++) *p++ = c;
    }
  }
  void fuellen(uint16_t c) { flaeche(ox, oy, w, h, c); }

  // Grenzen eines Rahmens auf den Beschnitt begrenzen.
  bool kasten(float x0, float y0, float x1, float y1, int &a, int &b, int &cc, int &d) {
    a = (int)floorf(x0); b = (int)floorf(y0); cc = (int)ceilf(x1); d = (int)ceilf(y1);
    if (a < kx0) a = kx0;
    if (b < ky0) b = ky0;
    if (cc > kx1) cc = kx1;
    if (d > ky1) d = ky1;
    return a < cc && b < d;
  }

  // Abgerundetes Rechteck, gefüllt.
  void rund(float x, float y, float rw, float rh, float r, uint16_t c) {
    int a, b, e, f;
    if (!kasten(x - 1, y - 1, x + rw + 1, y + rh + 1, a, b, e, f)) return;
    float mx = x + rw / 2, my = y + rh / 2, hx = rw / 2 - r, hy = rh / 2 - r;
    for (int xx = a; xx < e; xx++) for (int yy = b; yy < f; yy++) {
      float qx = fabsf(xx + 0.5f - mx) - hx, qy = fabsf(yy + 0.5f - my) - hy;
      float ax = qx > 0 ? qx : 0, ay = qy > 0 ? qy : 0;
      float d = sqrtf(ax * ax + ay * ay) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - r;
      abstand(xx, yy, d, c);
    }
  }
  // Abgerundetes Rechteck halb durchsichtig darüberlegen (Deckung al von 256) -
  // Rückmeldung beim Drücken eines Knopfs.
  void hauch(float x, float y, float rw, float rh, float r, uint16_t c, int al) {
    int a, b, e, f;
    if (!kasten(x, y, x + rw, y + rh, a, b, e, f)) return;
    float mx = x + rw / 2, my = y + rh / 2, hx = rw / 2 - r, hy = rh / 2 - r;
    for (int xx = a; xx < e; xx++) for (int yy = b; yy < f; yy++) {
      float qx = fabsf(xx + 0.5f - mx) - hx, qy = fabsf(yy + 0.5f - my) - hy;
      float ax = qx > 0 ? qx : 0, ay = qy > 0 ? qy : 0;
      float d = sqrtf(ax * ax + ay * ay) + (qx > qy ? (qx < 0 ? qx : 0) : (qy < 0 ? qy : 0)) - r;
      if (d < 0.5f) misch(xx, yy, c, d <= -0.5f ? al : (int)((0.5f - d) * al));
    }
  }
  // Karte: 1 px Rand in Farbe rand, innen grund - wie border + background in CSS.
  void karte(float x, float y, float rw, float rh, float r, uint16_t grund, uint16_t rand) {
    rund(x, y, rw, rh, r, rand);
    rund(x + 1, y + 1, rw - 2, rh - 2, r - 1, grund);
  }

  void scheibe(float cx, float cy, float r, uint16_t c) {
    int a, b, e, f;
    if (!kasten(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, a, b, e, f)) return;
    for (int xx = a; xx < e; xx++) for (int yy = b; yy < f; yy++) {
      float dx = xx + 0.5f - cx, dy = yy + 0.5f - cy;
      abstand(xx, yy, sqrtf(dx * dx + dy * dy) - r, c);
    }
  }
  // Kreislinie mit Mittelradius r und Strichstärke s (wie SVG stroke).
  void ring(float cx, float cy, float r, float s, uint16_t c) {
    float ra = r + s / 2;
    int a, b, e, f;
    if (!kasten(cx - ra - 1, cy - ra - 1, cx + ra + 1, cy + ra + 1, a, b, e, f)) return;
    for (int xx = a; xx < e; xx++) for (int yy = b; yy < f; yy++) {
      float dx = xx + 0.5f - cx, dy = yy + 0.5f - cy;
      abstand(xx, yy, fabsf(sqrtf(dx * dx + dy * dy) - r) - s / 2, c);
    }
  }
  // Bogen mit runden Enden. Winkel in Grad, 0 = oben, im Uhrzeigersinn,
  // a1 > a0. Entspricht einem SVG-Strich mit stroke-linecap="round".
  void bogen(float cx, float cy, float r, float s, float a0, float a1, uint16_t c) {
    if (a1 - a0 >= 359.9f) { ring(cx, cy, r, s, c); return; }
    if (a1 <= a0) return;
    float ra = r + s / 2, hs = s / 2;
    float w0 = a0 * (float)M_PI / 180, w1 = a1 * (float)M_PI / 180;
    float k0x = cx + r * sinf(w0), k0y = cy - r * cosf(w0);
    float k1x = cx + r * sinf(w1), k1y = cy - r * cosf(w1);
    int a, b, e, f;
    if (!kasten(cx - ra - 1, cy - ra - 1, cx + ra + 1, cy + ra + 1, a, b, e, f)) return;
    for (int xx = a; xx < e; xx++) for (int yy = b; yy < f; yy++) {
      float px = xx + 0.5f, py = yy + 0.5f, dx = px - cx, dy = py - cy;
      float rho = sqrtf(dx * dx + dy * dy);
      if (fabsf(rho - r) > hs + 1) continue;
      float wi = atan2f(dx, -dy) * 180 / (float)M_PI;
      if (wi < 0) wi += 360;
      // Winkel in den Bereich ab a0 legen
      float rel = wi - a0; while (rel < 0) rel += 360; while (rel >= 360) rel -= 360;
      float d;
      if (rel <= a1 - a0) d = fabsf(rho - r) - hs;
      else {
        float e0 = sqrtf((px - k0x) * (px - k0x) + (py - k0y) * (py - k0y));
        float e1 = sqrtf((px - k1x) * (px - k1x) + (py - k1y) * (py - k1y));
        d = (e0 < e1 ? e0 : e1) - hs;
      }
      abstand(xx, yy, d, c);
    }
  }

  static inline float strecke(float px, float py, float x0, float y0, float x1, float y1) {
    float vx = x1 - x0, vy = y1 - y0, wx = px - x0, wy = py - y0;
    float l = vx * vx + vy * vy, t = l > 0 ? (wx * vx + wy * vy) / l : 0;
    t = klemm(t, 0, 1);
    float dx = wx - t * vx, dy = wy - t * vy;
    return sqrtf(dx * dx + dy * dy);
  }
  void linie(float x0, float y0, float x1, float y1, float s, uint16_t c) {
    float p[4] = {x0, y0, x1, y1};
    polylinie(p, 2, s, c, false);
  }
  // Linienzug mit runden Ecken und Enden. Jedes Pixel gehört dem nächstgelegenen
  // Teilstück; so wird an Knicken nichts doppelt gemischt.
  void polylinie(const float *p, int n, float s, uint16_t c, bool zu) {
    if (n < 2) return;
    int teile = zu ? n : n - 1;
    float hs = s / 2;
    for (int i = 0; i < teile; i++) {
      float x0 = p[2 * i], y0 = p[2 * i + 1];
      float x1 = p[2 * ((i + 1) % n)], y1 = p[2 * ((i + 1) % n) + 1];
      int a, b, e, f;
      if (!kasten(fminf(x0, x1) - hs - 1, fminf(y0, y1) - hs - 1,
                  fmaxf(x0, x1) + hs + 1, fmaxf(y0, y1) + hs + 1, a, b, e, f)) continue;
      for (int xx = a; xx < e; xx++) for (int yy = b; yy < f; yy++) {
        float px = xx + 0.5f, py = yy + 0.5f;
        float d = strecke(px, py, x0, y0, x1, y1);
        if (d > hs + 0.5f) continue;
        bool eigen = true;
        for (int j = i - 3; j <= i + 3 && eigen; j++) {
          if (j == i) continue;
          int jj = j;
          if (zu) jj = (j + teile) % teile; else if (j < 0 || j >= teile) continue;
          float dj = strecke(px, py, p[2 * jj], p[2 * jj + 1],
                             p[2 * ((jj + 1) % n)], p[2 * ((jj + 1) % n) + 1]);
          if (dj < d || (dj == d && jj < i)) eigen = false;
        }
        if (eigen) abstand(xx, yy, d - hs, c);
      }
    }
  }
  // Gefülltes Vieleck (gerade-ungerade-Regel), Kanten weich.
  void vieleck(const float *p, int n, uint16_t c) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
      x0 = fminf(x0, p[2 * i]); x1 = fmaxf(x1, p[2 * i]);
      y0 = fminf(y0, p[2 * i + 1]); y1 = fmaxf(y1, p[2 * i + 1]);
    }
    int a, b, e, f;
    if (!kasten(x0 - 1, y0 - 1, x1 + 1, y1 + 1, a, b, e, f)) return;
    for (int xx = a; xx < e; xx++) for (int yy = b; yy < f; yy++) {
      float px = xx + 0.5f, py = yy + 0.5f, d = 1e9f;
      bool innen = false;
      for (int i = 0, j = n - 1; i < n; j = i++) {
        float xi = p[2 * i], yi = p[2 * i + 1], xj = p[2 * j], yj = p[2 * j + 1];
        if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) innen = !innen;
        float dd = strecke(px, py, xi, yi, xj, yj);
        if (dd < d) d = dd;
      }
      abstand(xx, yy, innen ? -d : d, c);
    }
  }

  // ---------- Text ----------
  static uint32_t naechstes(const char *&s) {
    uint8_t c = (uint8_t)*s++;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0) { uint32_t v = (c & 0x1F) << 6; v |= (*s++ & 0x3F); return v; }
    if ((c & 0xF0) == 0xE0) {
      uint32_t v = (c & 0x0F) << 12; v |= (*s++ & 0x3F) << 6; v |= (*s++ & 0x3F); return v;
    }
    s += 3; return '?';
  }
  static const Glyphe *suche(const Schrift &f, uint32_t cp) {
    int lo = 0, hi = f.n - 1;
    while (lo <= hi) {
      int m = (lo + hi) / 2;
      if (f.gl[m].cp == cp) return &f.gl[m];
      if (f.gl[m].cp < cp) lo = m + 1; else hi = m - 1;
    }
    return cp == '?' ? nullptr : suche(f, '?');
  }
  // tab: Ziffern in fester Breite (tabular-nums), für Werte, die sich ändern
  static float breite(const char *s, const Schrift &f, bool tab = false) {
    float b = 0;
    while (*s) {
      uint32_t cp = naechstes(s); const Glyphe *g = suche(f, cp);
      if (g) b += (tab && cp >= '0' && cp <= '9' ? f.ziffer : g->vor) / 16.0f;
    }
    return b;
  }
  // Schreibt ab Grundlinie y. anker: 0 links, 1 mitte, 2 rechts. Gibt die Breite zurück.
  float text(float x, float y, const char *s, const Schrift &f, uint16_t c, int anker = 0, bool tab = false) {
    float b = breite(s, f, tab);
    if (anker == 1) x -= b / 2; else if (anker == 2) x -= b;
    float cx = x;
    int gy = (int)lroundf(y);
    while (*s) {
      uint32_t cp = naechstes(s);
      const Glyphe *g = suche(f, cp);
      if (!g) continue;
      bool z = tab && cp >= '0' && cp <= '9';
      float vor = (z ? f.ziffer : g->vor) / 16.0f;
      int gx = (int)lroundf(cx + (z ? (vor - g->vor / 16.0f) / 2 : 0)) + g->xo, oy0 = gy + g->yo;
      uint32_t i = g->ofs;
      for (int r = 0; r < g->h; r++) for (int k = 0; k < g->w; k++, i++) {
        int v = (f.px[i >> 1] >> ((i & 1) ? 0 : 4)) & 15;
        if (v) misch(gx + k, oy0 + r, c, v == 15 ? 256 : v * 17);
      }
      cx += vor;
    }
    return b;
  }
};
