#pragma once
// Alle Bildschirme des Monitors. Ohne Arduino-Abhängigkeit: Die Seiten bekommen
// eine Leinwand und den aktuellen Stand (Lage) und zeichnen. Was angetippt werden
// kann, tragen sie dabei als Knopf ein. Die Hardware-Seite (pvmonitor.ino) liest
// die Wünsche der Oberfläche (WLAN suchen, verbinden, Speicher suchen, ...) aus
// und erledigt sie im Netz-Task.
//
// Maße: Flussdiagramm = App-Koordinaten mal 480/350, alle anderen Seiten nach
// den Entwürfen im Design-Canvas (je 480 x 480).

#include "leinwand.h"
#include "schriften.h"
#include "ikonen.h"
#include "daten.h"
#include "texte.h"

// ---------- Farben (dunkles Thema der App) ----------
static const uint16_t C_BG = rgb(0x0A0C11), C_CARD = rgb(0x13161E), C_CARD2 = rgb(0x1B1F2A),
  C_LINE = rgb(0x252A36), C_LINE2 = rgb(0x1F2430), C_LINE3 = rgb(0x2A3040),
  C_TEXT = rgb(0xF2F4F8), C_MUTED = rgb(0x8E95A5), C_MUTED2 = rgb(0xC9CED8), C_DIM = rgb(0x6B7282),
  C_SOLAR = rgb(FARBE_DUNKEL_SOLAR), C_HOME = rgb(FARBE_DUNKEL_HOME), C_BAT = rgb(FARBE_DUNKEL_BAT),
  C_GRID = rgb(FARBE_DUNKEL_GRID), C_OK = rgb(0x4ADE80), C_WARN = rgb(0xF87171),
  C_LIMLO = rgb(0xFF6B81), C_LIMHI = rgb(0x6FA8FF),
  C_SOLARBG = rgb(0x1E1A12), C_SOLARLN = rgb(0x3A3124), C_GRIDBG = rgb(0x241319), C_GRIDLN = rgb(0x45242D),
  C_HOMEBG = rgb(0x18162A), C_HOMELN = rgb(0x2B2745), C_BATBG = rgb(0x112420), C_BATLN = rgb(0x21342F),
  C_ONBAT = rgb(0x052219), C_NACHT = rgb(0x12161F), C_TASTE = rgb(0x1B1F2A), C_SONDER = rgb(0x252A36);

// ---------- Zustand, den die Hardware liefert ----------
struct Netz { char ssid[33]; int rssi; bool offen; };
struct Fund { char host[48]; char ip[16]; char produkt[32]; };

static const int SLOTS = 288;            // 5-Minuten-Werte eines Tages
struct Verlauf {
  uint32_t tag = 0;                      // JJJJMMTT, 0 = leer
  int16_t solar[SLOTS], haus[SLOTS], laden[SLOTS];   // -1 = keine Daten
  float whSolar = 0, whHaus = 0, whNetz = 0;
  void leeren(uint32_t t) {
    tag = t; whSolar = whHaus = whNetz = 0;
    for (int i = 0; i < SLOTS; i++) solar[i] = haus[i] = laden[i] = -1;
  }
};

struct Einst {
  char ssid[33] = "", pass[65] = "", ip[16] = "", host[48] = "", produkt[32] = "";
  int peaks[4] = {200, 200, 200, 200};
  int cap = 0;                           // Wh, 0 = aus der Zahl der Packs
  int nachtHell = 20;                    // Prozent
  int zone = 0;
  int sprache = 0;
};

struct Lage {
  Werte w;
  int alterS = -1;                       // Sekunden seit dem letzten Bericht, -1 = noch keiner
  bool uhr = false; int hh = 0, mm = 0, ss = 0;
  Netz netze[24]; int netzAnz = 0; bool scanLaeuft = false;
  Fund funde[6]; int fundAnz = 0; bool sucheLaeuft = false;
  int verbinden = 0;                     // 0 nichts, 1 läuft, 2 ok, 3 Fehler
  int build = 0;
  Verlauf *verlauf = nullptr;
  // Update über WLAN
  int updBuild = 0;                      // neuestes Build auf GitHub, 0 = unbekannt
  int updStatus = 0;                     // 0 nichts, 1 prüft, 2 lädt, 3 fertig, 4 Fehler
  int updProzent = 0; bool updGeprueft = false; char updFehler[64] = "";
};

// ---------- Knöpfe ----------
enum KnopfId {
  K_PILLE = 1, K_PUNKT, K_NETZ, K_WEITERE, K_NAME, K_TASTE, K_ZEIGEN, K_VERBINDEN, K_ZURUECK,
  K_GERAET, K_NEU_SUCHEN, K_IP, K_SP_AENDERN, K_WLAN_AENDERN, K_FELD, K_SPRACHE, K_ZONE,
  K_REGLER, K_ZAHL, K_OK, K_NOCHMAL, K_NEU_NETZE, K_UPDATE, K_UPD_PRUEFEN, K_UPD_INSTALL
};
struct Knopf { int16_t x, y, w, h; uint8_t id; int16_t arg; };

enum Seite { S_FLUSS, S_ZUSTAND, S_VERLAUF, S_EINST, S_NETZE, S_TASTATUR, S_SPEICHER, S_ZAHL, S_MELDUNG, S_UPDATE };

// Wünsche an die Hardware
enum { W_SCAN = 1, W_VERBINDEN = 2, W_SUCHE = 4, W_SICHERN = 8, W_GERAET = 16, W_HELL = 32, W_ZONE = 64,
       W_UPD_PRUEFEN = 128, W_UPD_LADEN = 256 };

// Eine Leitung im Flussdiagramm mit ihren laufenden Strichen
struct Draht { float x0, y0, x1, y1; uint16_t farbe[3]; int anz; bool rueck; bool an; };

static const float K = 480.0f / 350.0f;
static inline float AX(float x) { return x * K; }
static inline float AY(float y) { return (y + 60) * K; }

// ---------- Hilfen ----------
static void fmtW(char *b, int n, float w) {
  if (isnan(w)) snprintf(b, n, "– W"); else snprintf(b, n, "%d W", (int)lroundf(w));
}
// Zahl mit Komma, Minus als echtes Minuszeichen
static void fmtKomma(char *b, int n, float v, int stellen) {
  char t[24]; snprintf(t, sizeof t, "%.*f", stellen, fabsf(v));
  for (char *p = t; *p; p++) if (*p == '.') *p = ',';
  snprintf(b, n, "%s%s", v < 0 && lroundf(v * powf(10, stellen)) != 0 ? "\xE2\x88\x92" : "", t);
}
static void ikone(Leinwand &L, const float *ik, float dx, float dy, float s, float strich, uint16_t c, bool fuellen = false) {
  int teile = (int)ik[0], i = 1;
  float pts[160];
  for (int t = 0; t < teile; t++) {
    int n = (int)ik[i], zu = (int)ik[i + 1]; i += 2;
    int m = n < 80 ? n : 80;
    for (int k = 0; k < m; k++) { pts[2 * k] = dx + ik[i + 2 * k] * s; pts[2 * k + 1] = dy + ik[i + 2 * k + 1] * s; }
    i += 2 * n;
    if (fuellen) L.vieleck(pts, m, c); else L.polylinie(pts, m, strich, c, zu);
  }
}

struct Oberflaeche {
  Seite seite = S_FLUSS;
  Knopf kn[96]; int knAnz = 0;
  uint32_t wuensche = 0;
  Einst *e = nullptr;
  Draht draht[8]; int drahtAnz = 0;
  bool neu = true;                    // ganze Seite neu zeichnen

  // Einrichtung und Eingaben
  int netzSeite = 0;                  // Blättern in der Netzliste
  char eingabe[65] = ""; bool zeigen = true;
  int tastModus = 0;                  // 0 klein, 1 groß, 2 Zeichen, 3 Zeichen 2
  int tastZweck = 0;                  // 0 Passwort, 1 Netzname
  char zielSsid[33] = "";
  int zahlFeld = 0;                   // 0..3 PV, 4 Kapazität, 5 IP
  char meldung[96] = ""; bool meldungFehler = false;
  bool einrichtung = false;           // erster Start: Schritte 1-3

  void knopf(float x, float y, float w, float h, uint8_t id, int arg = 0) {
    if (knAnz < 96) kn[knAnz++] = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, id, (int16_t)arg};
  }
  void gehe(Seite s) { seite = s; neu = true; }

  // ===================== gemeinsame Teile =====================
  // Lage der Uhr-Pille der aktuellen Seite, damit sie jede Sekunde allein
  // neu gezeichnet werden kann, ohne die ganze Seite.
  float pilleR = -1, pilleO = 0; bool pilleTeil = false;
  void pille(Leinwand &L, const Lage &g, float rechts, float oben) {
    pilleR = rechts; pilleO = oben;
    char t[16];
    if (g.uhr) snprintf(t, sizeof t, "%02d:%02d:%02d", g.hh, g.mm, g.ss); else snprintf(t, sizeof t, "--:--:--");
    float b = Leinwand::breite(t, M125, true), w = 11 + 8 + 6 + b + 11, x = rechts - w;
    L.karte(x, oben, w, 30, 15, C_CARD, C_LINE);
    uint16_t punkt = g.alterS < 0 ? C_DIM : g.alterS <= 15 ? C_OK : g.alterS <= 60 ? C_SOLAR : C_WARN;
    L.scheibe(x + 15, oben + 15, 4, punkt);
    L.text(x + 25, oben + 19.5f, t, M125, C_MUTED2, 0, true);
    if (!pilleTeil) knopf(x - 10, oben - 8, w + 20, 46, K_PILLE);
  }
  // Nur die Pille neu; gibt den Bereich zurück, der ins Bild kopiert werden muss.
  bool nurPille(Leinwand &L, const Lage &g, int &x, int &y, int &w, int &h) {
    if (pilleR < 0) return false;
    x = (int)pilleR - 140; y = (int)pilleO - 2; w = 142; h = 34;
    L.flaeche(x, y, w, h, C_BG);
    pilleTeil = true; pille(L, g, pilleR, pilleO); pilleTeil = false;
    return true;
  }
  // Seitenpunkte unten rechts: In der Mitte säße der mittlere Punkt im Akkuring.
  void punkte(Leinwand &L, int aktiv, float y) {
    (void)y; y = 461;
    float x0 = 460 - (3 * 7 + 2 * 8);
    for (int i = 0; i < 3; i++) {
      L.scheibe(x0 + i * 15 + 3.5f, y + 3.5f, 3.5f, i == aktiv ? C_TEXT : C_LINE3);
      knopf(x0 + i * 15 - 8, y - 12, 23, 31, K_PUNKT, i);
    }
  }
  void titel(Leinwand &L, const char *t, const char *unter, bool zurueck) {
    float x = 20;
    if (zurueck) {
      ikone(L, IK_ZURUECK, 14, 16, 1.2f, 2.2f, C_MUTED2);
      knopf(0, 0, 60, 56, K_ZURUECK);
      x = 48;
    }
    L.text(x, 44, t, S220, C_TEXT);
    if (unter) L.text(20, 66, unter, R130, C_MUTED);
  }
  void taste(Leinwand &L, float x, float y, float w, float h, const char *t, const Schrift &f,
             uint16_t grund, uint16_t farbe, uint8_t id, int arg) {
    L.rund(x, y, w, h, 11, grund);
    if (t && *t) L.text(x + w / 2, y + h / 2 + f.gross / 2.0f, t, f, farbe, 1);
    knopf(x, y, w, h, id, arg);
  }

  // ===================== Fluss =====================
  void fluss(Leinwand &L, const Lage &g) {
    const Werte &w = g.w;
    Fluss f = rechneFluss(w);
    char b[48];
    L.text(16, 22, T(T_ENERGIEFLUSS), M178, C_MUTED);
    pille(L, g, 464, 8);
    drahtAnz = 0;
    if (!w.da) {
      L.text(240, 250, T(g.alterS < 0 ? T_WARTE : T_KEINE_DATEN), R180, C_MUTED, 1);
      punkte(L, 0, 459);
      return;
    }
    float hausW = f.home, netzW = f.grid, bezug = f.grid;
    // Leitungen (Grund)
    auto grundLinie = [&](float x0, float y0, float x1, float y1) {
      L.linie(AX(x0), AY(y0), AX(x1), AY(y1), 3 * K, C_LINE);
    };
    if (!f.ohnePv) grundLinie(175, 70, 175, 118);
    grundLinie(72, 142, 152, 142); grundLinie(198, 142, 278, 142); grundLinie(175, 166, 175, 220);
    if (f.hatOff) grundLinie(144, 248, 66, 248);

    // Solar
    if (!f.ohnePv) {
      L.text(AX(175), AY(-44), T(T_SOLAR), R165, C_MUTED, 1);
      fmtW(b, sizeof b, f.solar); L.text(AX(175), AY(-12), b, S261, C_TEXT, 1, true);
      L.scheibe(AX(175), AY(42), 30.5f * K, C_SOLARBG);
      int n = f.pvAnz; float gap = n == 2 ? 12 : 10;
      for (int i = 0; i < n; i++) {
        float a0 = i * 360.0f / n + gap / 2, a1 = (i + 1) * 360.0f / n - gap / 2;
        L.bogen(AX(175), AY(42), 28 * K, 5 * K, a0, a1, C_SOLARLN);
        float pk = i < 4 ? e->peaks[i] : 0;
        float v = isnan(f.pv[i]) ? 0 : f.pv[i];
        float q = klemm(v / fmaxf(25, pk), 0, 1);
        if (q > 0.012f) L.bogen(AX(175), AY(42), 28 * K, 5 * K, a0, a0 + q * (a1 - a0), C_SOLAR);
      }
      ikone(L, IK_SOLAR, AX(160), AY(27), 1.25f * K, 1.6f * 1.25f * K, C_SOLAR);
      // Beschriftung der Eingänge wie PV_POS / pvStellen() in der App
      for (int i = 0; i < n; i++) {
        float x, y; int anker;
        static const float P2[2][3] = {{213, 46, 0}, {137, 46, 2}};
        static const float P4[4][3] = {{213, 22, 0}, {213, 70, 0}, {137, 70, 2}, {137, 22, 2}};
        if (n == 2) { x = P2[i][0]; y = P2[i][1]; anker = (int)P2[i][2]; }
        else if (n == 4) { x = P4[i][0]; y = P4[i][1]; anker = (int)P4[i][2]; }
        else {
          float mitte = (i + 0.5f) * 360 / n, wi = (mitte - 90) * (float)M_PI / 180;
          x = roundf(175 + 44 * cosf(wi)); y = roundf(42 + 44 * sinf(wi) + 4);
          anker = fabsf(cosf(wi)) <= 0.05f ? 1 : cosf(wi) > 0.05f ? 0 : 2;
        }
        char z[16]; fmtW(z, sizeof z, f.pv[i]);
        snprintf(b, sizeof b, "PV%d %s", i + 1, z);
        L.text(AX(x), AY(y), b, M171, C_MUTED2, anker);
      }
    }
    // Nabe mit Verlust
    L.scheibe(AX(175), AY(142), 22 * K + 0.5f * K, C_LINE3);
    L.scheibe(AX(175), AY(142), 22 * K - 0.5f * K, C_CARD2);
    ikone(L, IK_BLITZ, 0, 60 * K, K, 0, C_TEXT, true);
    if (!isnan(f.verlust)) {
      char z[16]; fmtW(z, sizeof z, f.verlust); snprintf(b, sizeof b, "\xE2\x88\x92%s", z);
      L.text(AX(202), AY(137), b, R171, C_MUTED2);
    }
    // Ringe: Netz und Haus an der Gerätegrenze, Akku am Ladestand
    float grenzeAus = w.hat(F_INVMAX) && w.f[F_INVMAX] > 0 ? w.f[F_INVMAX] : 800;
    auto knoten = [&](float cx, float cy, float r, uint16_t bg, uint16_t ln, uint16_t farbe, float q) {
      L.scheibe(AX(cx), AY(cy), r * K, bg);
      L.ring(AX(cx), AY(cy), r * K, 5 * K, ln);
      q = klemm(q, 0, 1);
      if (q > 0.004f) L.bogen(AX(cx), AY(cy), r * K, 5 * K, 0, q * 360, farbe);
    };
    knoten(46, 142, 26, C_GRIDBG, C_GRIDLN, C_GRID,
           f.grid / fmaxf(50, w.hat(F_CHGMAX) && w.f[F_CHGMAX] > 0 ? w.f[F_CHGMAX] : grenzeAus));
    ikone(L, IK_MAST, 0, 60 * K, K, 2 * K, C_GRID);
    L.text(AX(46), AY(102), T(f.grid > 2 ? T_NETZ_AKKU : T_NETZ), R165, C_MUTED, 1);
    fmtW(b, sizeof b, netzW);
    L.text(AX(46), AY(192), b, ziffern(b) > 3 ? S199 : S233, netzW > 2 ? C_TEXT : C_MUTED, 1, true);

    knoten(304, 142, 26, C_HOMEBG, C_HOMELN, C_HOME, f.home / fmaxf(50, grenzeAus));
    ikone(L, IK_HAUS, AX(289), AY(127), 1.25f * K, 1.6f * 1.25f * K, C_HOME);
    L.text(AX(304), AY(102), T(T_INS_HAUS), R165, C_MUTED, 1);
    fmtW(b, sizeof b, hausW);
    L.text(AX(304), AY(192), b, ziffern(b) > 3 ? S199 : S233, C_TEXT, 1, true);

    // Off-Grid-Dose
    if (f.hatOff) {
      L.scheibe(AX(40), AY(248), 22 * K, C_HOMEBG);
      L.ring(AX(40), AY(248), 22 * K, 5 * K, C_HOMELN);
      ikone(L, IK_DOSE, AX(29), AY(237), K, 2 * K, C_HOME);
      L.text(AX(70), AY(234), T(T_OFFGRID), R165, C_MUTED);
      if (f.offW > 0) fmtW(b, sizeof b, f.offW); else snprintf(b, sizeof b, "%s", T(f.offBereit ? T_BEREIT : T_AUS));
      L.text(AX(70), AY(270), b, S233, f.offW > 0 ? C_TEXT : C_MUTED, 0, true);
    }
    // Akku
    knoten(175, 248, 28, C_BATBG, C_BATLN, C_BAT, f.soc / 100);
    snprintf(b, sizeof b, "%d%%", (int)f.soc);
    L.text(AX(175), AY(254), b, B219, C_TEXT, 1, true);
    auto marke = [&](float proz, uint16_t c) {
      if (proz <= 0.5f || proz >= 99.5f) return;
      float wi = (-90 + proz * 3.6f) * (float)M_PI / 180;
      L.linie(AX(175 + 24 * cosf(wi)), AY(248 + 24 * sinf(wi)), AX(175 + 32 * cosf(wi)), AY(248 + 32 * sinf(wi)), 2.5f * K, c);
    };
    marke(f.minSoc, C_LIMLO); marke(f.maxSoc, C_LIMHI);
    L.text(AX(216), AY(224), T(f.batZustand == 1 ? T_AKKU_LAEDT : f.batZustand == 2 ? T_AKKU_ENTLAEDT : T_AKKU_RUHT), R165, C_MUTED);
    fmtW(b, sizeof b, f.batW); L.text(AX(216), AY(248), b, S261, C_TEXT, 0, true);
    // Unter der Akkuleistung: wie lange noch
    int cap = kapazitaet(w);
    char unten[64], kw[16];
    fmtKomma(kw, sizeof kw, cap * f.soc / 100 / 1000, 2);
    snprintf(unten, sizeof unten, T(T_GESPEICHERT_FMT), kw);
    if (f.restMin >= 0) zeitText(unten, sizeof unten, f.restMin, T_NOCH_HM, T_NOCH_M);
    else if (f.laden > 0 && cap && f.soc < f.maxSoc) {
      float min = cap * (f.maxSoc - f.soc) / 100 / f.laden * 60;
      if (min < 2880) zeitText(unten, sizeof unten, (int)min, T_VOLL_HM, T_VOLL_M);
    }
    L.text(AX(216), AY(270), unten, R171, C_MUTED2);

    // Laufende Striche: Farbe der Quelle, wie setWire() in der App
    uint16_t q[3]; int qn = 0;
    struct QW { float w; uint16_t c; } qs[3] = {{f.solar, C_SOLAR}, {bezug, C_GRID}, {f.entladen, C_BAT}};
    for (int i = 0; i < 3; i++) for (int j = i + 1; j < 3; j++) if (qs[j].w > qs[i].w) { QW t = qs[i]; qs[i] = qs[j]; qs[j] = t; }
    for (int i = 0; i < 3; i++) if (qs[i].w > 2) q[qn++] = qs[i].c;
    uint16_t ohneAkku[3]; int on = 0;
    for (int i = 0; i < qn; i++) if (q[i] != C_BAT && on < 2) ohneAkku[on++] = q[i];
    auto neuerDraht = [&](float x0, float y0, float x1, float y1, float watt, bool rueck, const uint16_t *c, int n) {
      Draht &d = draht[drahtAnz++];
      d = {AX(x0), AY(y0), AX(x1), AY(y1), {c[0], n > 1 ? c[1] : c[0], n > 2 ? c[2] : c[0]}, n, rueck, watt > FLUSS_MINWATT};
    };
    if (!f.ohnePv) neuerDraht(175, 70, 175, 118, f.solar, false, &C_SOLAR, 1);
    neuerDraht(72, 142, 152, 142, netzW, false, &C_GRID, 1);
    if (f.hatOff) neuerDraht(144, 248, 66, 248, f.offW, false, &C_BAT, 1);
    if (f.entladen > 0) neuerDraht(175, 166, 175, 220, f.batW, true, &C_BAT, 1);
    else if (on) neuerDraht(175, 166, 175, 220, f.batW, false, ohneAkku, on);
    else neuerDraht(175, 166, 175, 220, f.batW, false, &C_BAT, 1);
    if (qn) neuerDraht(198, 142, 278, 142, hausW, false, q, qn);
    else neuerDraht(198, 142, 278, 142, hausW, false, &C_HOME, 1);
    punkte(L, 0, 459);
  }
  static int ziffern(const char *s) { int n = 0; for (; *s; s++) if (*s >= '0' && *s <= '9') n++; return n; }
  void zeitText(char *b, int n, int min, int hm, int m) {
    int h = min / 60, mi = min % 60;
    if (h) snprintf(b, n, T(hm), h, mi); else snprintf(b, n, T(m), mi);
  }
  int kapazitaet(const Werte &w) {
    if (e->cap > 0) return e->cap;
    return w.n(F_PACKNUM) > 0 ? (int)w.f[F_PACKNUM] * 1920 : 0;
  }
  // Bereich, den ein Draht samt Strichen braucht (für das Teil-Neuzeichnen)
  static void drahtRahmen(const Draht &d, int &x, int &y, int &w, int &h) {
    float r = 3 * K / 2 + 2;
    x = (int)floorf(fminf(d.x0, d.x1) - r); y = (int)floorf(fminf(d.y0, d.y1) - r);
    w = (int)ceilf(fmaxf(d.x0, d.x1) + r) - x; h = (int)ceilf(fmaxf(d.y0, d.y1) + r) - y;
  }
  // Striche eines Drahtes zum Zeitpunkt ms - dieselbe Uhr wie flussUhr() in der App
  static void striche(Leinwand &L, const Draht &d, uint32_t ms) {
    if (!d.an) return;
    float len = hypotf(d.x1 - d.x0, d.y1 - d.y0), ux = (d.x1 - d.x0) / len, uy = (d.y1 - d.y0) / len;
    float P = FLUSS_PERIODE * K, S = FLUSS_STRICHLAENGE * K, grund = (ms % FLUSS_DAUERMS) / (float)FLUSS_DAUERMS * P;
    for (int platz = 0; platz < d.anz; platz++) {
      float weg = d.rueck ? -grund : grund;
      float start = fmodf(weg + P * platz / d.anz, P); if (start < 0) start += P;
      for (float a = start - P; a < len; a += P) {
        float s0 = fmaxf(a, 0), s1 = fminf(a + S, len);
        if (s1 < s0) continue;
        L.linie(d.x0 + ux * s0, d.y0 + uy * s0, d.x0 + ux * s1, d.y0 + uy * s1, FLUSS_STRICHBREITE * K, d.farbe[platz]);
      }
    }
  }

  // ===================== Zustand =====================
  struct Zeile { const char *l; char v[24]; uint16_t c; };
  float zKarte(Leinwand &L, float x, float y, float w, const char *kopf, Zeile *z, int n) {
    float h = 10 + 18 + 4 + n * 30 + 6;
    L.karte(x, y, w, h, 18, C_CARD, C_LINE2);
    L.text(x + 16, y + 24, kopf, R130, C_MUTED);
    for (int i = 0; i < n; i++) {
      float zy = y + 32 + i * 30;
      if (i) L.flaeche((int)x + 16, (int)zy, (int)w - 32, 1, C_LINE);
      L.text(x + 16, zy + 20, z[i].l, R130, C_MUTED);
      L.text(x + w - 16, zy + 20, z[i].v, S150, z[i].c, 2, true);
    }
    return h;
  }
  void zustand(Leinwand &L, const Lage &g) {
    const Werte &w = g.w;
    L.text(20, 30, T(T_ZUSTAND), R130, C_MUTED);
    pille(L, g, 460, 10);
    if (!w.da) { L.text(240, 250, T(g.alterS < 0 ? T_WARTE : T_KEINE_DATEN), R180, C_MUTED, 1); punkte(L, 1, 459); return; }
    Fluss f = rechneFluss(w);
    char kopf[64]; Zeile z[6]; int n;
    float x0 = 20, bw = (440 - 12) / 2.0f, y = 52;
    // Solar
    n = 0;
    for (int i = 0; i < f.pvAnz && n < 4; i++) {
      static char lab[6][6]; snprintf(lab[i], 6, "PV%d", i + 1);
      z[n].l = lab[i]; fmtW(z[n].v, 24, f.pv[i]); z[n].c = C_TEXT; n++;
    }
    if (sprache) snprintf(kopf, sizeof kopf, "%s · %d %s", T(T_SOLAR), f.pvAnz, ruZahl(f.pvAnz, "вход", "входа", "входов"));
    else snprintf(kopf, sizeof kopf, "%s · %d Eingänge", T(T_SOLAR), f.pvAnz);
    float h1 = f.pvAnz ? zKarte(L, x0, y, bw, kopf, z, n) : 0;
    // Akku
    int packs = (int)w.n(F_PACKNUM), cap = kapazitaet(w);
    if (sprache) snprintf(kopf, sizeof kopf, "АКБ · %d %s", packs, ruZahl(packs, "блок", "блока", "блоков"));
    else snprintf(kopf, sizeof kopf, "Akku · %d Pack%s", packs, packs == 1 ? "" : "s");
    n = 0; char t[24];
    z[n].l = T(T_GESPEICHERT); fmtKomma(t, sizeof t, cap * f.soc / 100000.0f, 2);
    snprintf(z[n].v, 24, "%s %s", t, T(T_KWH)); z[n++].c = C_TEXT;
    float volt = !isnan(w.pk[P_TOTALVOL]) && w.pk[P_TOTALVOL] ? w.pk[P_TOTALVOL] : w.f[F_BATVOLT];
    z[n].l = T(T_PACKSPANNUNG);
    if (isnan(volt)) snprintf(z[n].v, 24, "–"); else { fmtKomma(t, sizeof t, volt / UM_SPANNUNGTEILER, 2); snprintf(z[n].v, 24, "%s V", t); }
    z[n++].c = C_TEXT;
    z[n].l = T(T_AKKUSTROM);
    if (isnan(w.pk[P_BATCUR])) snprintf(z[n].v, 24, "–");
    else {
      float roh = w.pk[P_BATCUR]; if (roh > UM_VORZEICHEN16BITGRENZE) roh -= UM_VORZEICHEN16BITABZUG;
      fmtKomma(t, sizeof t, roh / UM_STROMTEILER, 1); snprintf(z[n].v, 24, "%s A", t);
    }
    z[n++].c = C_TEXT;
    z[n].l = T(T_ENTLADEN_BIS);
    if (w.hat(F_MINSOC)) snprintf(z[n].v, 24, "%d %%", (int)(w.f[F_MINSOC] / 10)); else snprintf(z[n].v, 24, "–");
    z[n++].c = C_TEXT;
    float h2 = zKarte(L, f.pvAnz ? x0 + bw + 12 : x0, y, f.pvAnz ? bw : 440, kopf, z, n);
    y += fmaxf(h1, h2) + 12;
    // Zellen
    n = 0;
    float mx = w.pk[P_MAXVOL], mn = w.pk[P_MINVOL];
    z[n].l = T(T_HOECHSTE); if (isnan(mx)) snprintf(z[n].v, 24, "–"); else { fmtKomma(t, sizeof t, mx / 100, 2); snprintf(z[n].v, 24, "%s V", t); } z[n++].c = C_TEXT;
    z[n].l = T(T_NIEDRIGSTE); if (isnan(mn)) snprintf(z[n].v, 24, "–"); else { fmtKomma(t, sizeof t, mn / 100, 2); snprintf(z[n].v, 24, "%s V", t); } z[n++].c = C_TEXT;
    z[n].l = T(T_SPREIZUNG);
    if (isnan(mx) || isnan(mn) || mx <= 0) { snprintf(z[n].v, 24, "–"); z[n].c = C_TEXT; }
    else {
      int sp = (int)lroundf(mx - mn);
      snprintf(z[n].v, 24, "%d %s", sp * 10, sprache ? "мВ" : "mV");
      z[n].c = sp <= 3 ? C_OK : sp <= 7 ? C_TEXT : C_WARN;
    }
    n++;
    int zellen = 0;
    if (!isnan(mx) && !isnan(mn) && !isnan(w.pk[P_TOTALVOL]) && mx + mn > 0) zellen = (int)lroundf(w.pk[P_TOTALVOL] / ((mx + mn) / 2));
    if (zellen >= 4 && zellen <= 32) { char r[32]; snprintf(r, sizeof r, T(T_IN_REIHE), zellen); snprintf(kopf, sizeof kopf, "%s · %s", T(T_ZELLEN), r); }
    else snprintf(kopf, sizeof kopf, "%s", T(T_ZELLEN));
    float h3 = zKarte(L, x0, y, bw, kopf, z, n);
    // Temperatur
    n = 0;
    float te = tempC(w.f[F_TEMP]), tp = tempC(w.pk[P_MAXTEMP]);
    z[n].l = T(T_WECHSELRICHTER); if (isnan(te)) snprintf(z[n].v, 24, "– °C"); else snprintf(z[n].v, 24, "%d °C", (int)lroundf(te)); z[n++].c = C_TEXT;
    z[n].l = T(T_AKKU); if (isnan(tp)) snprintf(z[n].v, 24, "– °C"); else snprintf(z[n].v, 24, "%d °C", (int)lroundf(tp)); z[n++].c = C_TEXT;
    if (w.hat(F_FANMODE) || w.hat(F_FANSPEED)) {
      z[n].l = T(T_LUEFTER);
      if (w.n(F_FANSPEED) > 0) snprintf(z[n].v, 24, T(T_STUFE), (int)w.f[F_FANSPEED]); else snprintf(z[n].v, 24, "%s", T(T_AUS));
      z[n++].c = C_TEXT;
    } else if (w.hat(F_HEAT)) {
      z[n].l = T(T_HEIZUNG); snprintf(z[n].v, 24, "%s", T(w.f[F_HEAT] ? T_AN : T_AUS));
      z[n++].c = w.f[F_HEAT] ? C_WARN : C_TEXT;
    }
    float h4 = zKarte(L, x0 + bw + 12, y, bw, T(T_TEMPERATUR), z, n);
    y += fmaxf(h3, h4) + 12;
    // Untere Karte: Modus, Ausgangslimit, WLAN
    L.karte(x0, y, 440, 62, 18, C_CARD, C_LINE2);
    const char *modus = w.n(F_ACMODE) == 1 ? T(T_MODUS_NETZ) : w.n(F_ACMODE) == 2 ? T(T_MODUS_HAUS) : "–";
    L.text(x0 + 16, y + 24, T(T_MODUS), R130, C_MUTED);
    L.text(x0 + 16, y + 48, modus, S160, C_TEXT);
    char v[24];
    L.text(x0 + 300, y + 24, T(T_AUSGANGSLIMIT), R130, C_MUTED, 2);
    fmtW(v, sizeof v, w.hat(F_OUTLIMIT) ? w.f[F_OUTLIMIT] : NAN); L.text(x0 + 300, y + 48, v, S160, C_TEXT, 2, true);
    L.text(x0 + 424, y + 24, T(T_WLAN), R130, C_MUTED, 2);
    if (w.hat(F_RSSI) && w.f[F_RSSI]) snprintf(v, sizeof v, "\xE2\x88\x92%d dBm", (int)fabsf(w.f[F_RSSI])); else snprintf(v, sizeof v, "–");
    L.text(x0 + 424, y + 48, v, S160, C_TEXT, 2, true);
    punkte(L, 1, 459);
  }

  // ===================== Verlauf =====================
  void verlauf(Leinwand &L, const Lage &g) {
    L.text(20, 30, T(T_HEUTE), R130, C_MUTED);
    pille(L, g, 464, 10);
    // Legende
    float lx = 20;
    struct { uint16_t c; int t; } leg[3] = {{C_SOLAR, T_SOLAR}, {C_HOME, T_INS_HAUS}, {C_BAT, T_LEG_LAEDT}};
    for (int i = 0; i < 3; i++) {
      L.rund(lx, 46, 9, 9, 3, leg[i].c);
      lx += 15 + L.text(lx + 15, 55, T(leg[i].t), R125, C_MUTED2) + 16;
    }
    const Verlauf *v = g.verlauf;
    int jetzt = g.uhr ? (g.hh * 60 + g.mm) / 5 : -1;
    // Massstab: vier Abschnitte, runde Schritte
    int maxW = 0;
    if (v && v->tag) for (int i = 0; i < SLOTS; i++) {
      int m = v->solar[i]; if (v->haus[i] > m) m = v->haus[i]; if (v->laden[i] > m) m = v->laden[i];
      if (m > maxW) maxW = m;
    }
    static const int SCHRITTE[] = {50, 100, 150, 200, 250, 300, 400, 500, 600, 750, 1000, 1500, 2000, 2500};
    int schritt = 2500;
    for (int s : SCHRITTE) if (s * 4 >= maxW) { schritt = s; break; }
    const float X0 = 44, X1 = 456, Y0 = 116, Y1 = 356;
    auto sx = [&](float slot) { return X0 + (X1 - X0) * slot / SLOTS; };
    auto sy = [&](float watt) { return Y1 - (Y1 - Y0) * watt / (schritt * 4.0f); };
    // Nacht: vor dem ersten und nach dem letzten Solarwert
    if (v && v->tag) {
      int auf = -1, unter = -1;
      for (int i = 0; i < SLOTS; i++) if (v->solar[i] > 5) { if (auf < 0) auf = i; unter = i; }
      if (auf < 0 && jetzt >= 0) L.flaeche((int)X0, (int)Y0, (int)(sx(jetzt) - X0), (int)(Y1 - Y0), C_NACHT);
      if (auf > 0) L.flaeche((int)X0, (int)Y0, (int)(sx(auf) - X0), (int)(Y1 - Y0), C_NACHT);
      if (unter >= 0 && jetzt >= 0 && jetzt - unter >= 6) L.flaeche((int)sx(unter + 1), (int)Y0, (int)(X1 - sx(unter + 1)), (int)(Y1 - Y0), C_NACHT);
    }
    for (int i = 0; i <= 4; i++) {
      float yy = Y0 + i * 60;
      L.flaeche((int)X0, (int)yy, (int)(X1 - X0), 1, i == 4 ? C_LINE : C_LINE2);
      char t[12]; snprintf(t, sizeof t, "%d", schritt * (4 - i));
      L.text(38, yy + 4, t, R105, C_DIM, 2);
    }
    static const char *STD[] = {"00", "06", "12", "18", "24"};
    for (int i = 0; i < 5; i++) L.text(X0 + (X1 - X0) * i / 4, 372, STD[i], R105, C_DIM, i == 4 ? 2 : 0);
    if (!v || !v->tag) {
      L.text(240, 240, T(T_VERLAUF_WARTET), R140, C_MUTED, 1);
    } else {
      // Akku lädt: Fläche mit 22 % Deckung
      uint16_t flaeche = mische(C_BAT, C_BG, 56);
      for (int i = 0; i < SLOTS; i++) if (v->laden[i] > 0) {
        float ya = sy(v->laden[i]);
        int xa = (int)sx(i), xb = (int)sx(i + 1);
        L.flaeche(xa, (int)ya, xb - xa, (int)(Y1 - ya), flaeche);
      }
      kurve(L, v->solar, sx, sy, C_SOLAR);
      kurve(L, v->haus, sx, sy, C_HOME);
      if (jetzt >= 0) {
        float xj = sx(jetzt + 0.5f);
        for (float yy = Y0; yy < Y1; yy += 7) L.flaeche((int)xj, (int)yy, 1, 3, C_MUTED);
        int hz = v->haus[jetzt] >= 0 ? v->haus[jetzt] : (jetzt > 0 ? v->haus[jetzt - 1] : -1);
        if (hz >= 0) L.scheibe(xj, sy(hz), 3.5f, C_HOME);
      }
    }
    // Tagesbilanz
    struct { int t; float wh; uint16_t c; } bil[3] = {
      {T_SOLAR_HEUTE, v ? v->whSolar : 0, C_SOLAR}, {T_INS_HAUS, v ? v->whHaus : 0, C_HOME}, {T_AUS_NETZ, v ? v->whNetz : 0, C_GRID}};
    for (int i = 0; i < 3; i++) {
      float x = 20 + i * (140 + 10);
      L.karte(x, 396, 140, 58, 16, C_CARD, C_LINE2);
      L.text(x + 14, 417, T(bil[i].t), R115, C_MUTED);
      char t[24], z[16]; fmtKomma(z, sizeof z, bil[i].wh / 1000, 1);
      snprintf(t, sizeof t, "%s %s", z, T(T_KWH));
      L.text(x + 14, 443, t, S180, bil[i].c, 0, true);
    }
    punkte(L, 2, 459);
  }
  static uint16_t mische(uint16_t a, uint16_t b, int t) {   // t/256 von a
    int r = ((a >> 11) * t + (b >> 11) * (256 - t)) >> 8;
    int gg = (((a >> 5) & 63) * t + ((b >> 5) & 63) * (256 - t)) >> 8;
    int bl = ((a & 31) * t + (b & 31) * (256 - t)) >> 8;
    return (uint16_t)((r << 11) | (gg << 5) | bl);
  }
  template <class FX, class FY>
  void kurve(Leinwand &L, const int16_t *d, FX sx, FY sy, uint16_t c) {
    static float p[2 * SLOTS];
    int n = 0;
    for (int i = 0; i <= SLOTS; i++) {
      if (i < SLOTS && d[i] >= 0) { p[2 * n] = sx(i + 0.5f); p[2 * n + 1] = sy(d[i]); n++; continue; }
      if (n == 1) L.scheibe(p[0], p[1], 1.25f, c); else if (n > 1) L.polylinie(p, n, 2.5f, c, false);
      n = 0;
    }
  }

  // ===================== Einstellungen =====================
  void einst(Leinwand &L, const Lage &g) {
    ikone(L, IK_ZURUECK, 12, 10, 1.1f, 2.2f, C_MUTED2);
    knopf(0, 0, 230, 48, K_ZURUECK);
    L.text(44, 33, T(T_EINSTELLUNGEN), S200, C_TEXT);
    char b[64];
    if (g.updBuild > g.build) {
      snprintf(b, sizeof b, T(T_UPD_PILLE), g.updBuild);
      float bw = Leinwand::breite(b, S150) + 24;
      L.rund(460 - bw, 13, bw, 30, 15, C_BAT);
      L.text(460 - bw / 2, 33, b, S150, C_ONBAT, 1);
      knopf(450 - bw, 4, bw + 20, 46, K_UPDATE);
    } else {
      snprintf(b, sizeof b, "Build %d", g.build); L.text(460, 32, b, R130, C_MUTED, 2);
      knopf(360, 4, 120, 46, K_UPDATE);
    }
    // Speicher und WLAN
    float y = 50;
    L.karte(20, y, 440, 118, 16, C_CARD, C_LINE2);
    char name[40]; produktName(e->produkt[0] ? e->produkt : g.w.produkt, name, sizeof name);
    snprintf(b, sizeof b, "%s · %s", T(T_SPEICHER), e->ip[0] ? e->ip : "–");
    L.text(34, y + 23, b, R120, C_MUTED);
    L.text(34, y + 46, e->ip[0] ? name : "–", S160, C_TEXT);
    taste(L, 340, y + 11, 106, 38, T(T_AENDERN), R140, C_CARD2, C_MUTED2, K_SP_AENDERN, 0);
    L.flaeche(34, (int)y + 59, 412, 1, C_LINE);
    L.text(34, y + 82, T(T_WLAN), R120, C_MUTED);
    L.text(34, y + 105, e->ssid[0] ? e->ssid : "–", S160, C_TEXT);
    taste(L, 340, y + 70, 106, 38, T(T_AENDERN), R140, C_CARD2, C_MUTED2, K_WLAN_AENDERN, 0);
    // Modulleistung und Kapazität
    y = 176;
    L.karte(20, y, 440, 142, 16, C_CARD, C_LINE2);
    L.text(34, y + 22, T(T_MODULLEISTUNG), R120, C_MUTED);
    Fluss f = rechneFluss(g.w);
    int n = g.w.da && f.pvAnz ? (f.pvAnz > 4 ? 4 : f.pvAnz) : 4;
    float fw = (412 - (n - 1) * 8.0f) / n;
    for (int i = 0; i < n; i++) {
      float x = 34 + i * (fw + 8);
      snprintf(b, sizeof b, "PV%d", i + 1); L.text(x, y + 46, b, R120, C_MUTED);
      snprintf(b, sizeof b, "%d", e->peaks[i]);
      feld(L, x, y + 52, fw, b, K_FELD, i, false);
    }
    L.text(34, y + 123, T(T_KAPAZITAET), R120, C_MUTED);
    snprintf(b, sizeof b, "%d", e->cap > 0 ? e->cap : kapazitaet(g.w));
    feld(L, 318, y + 98, 128, b, K_FELD, 4, true);
    // Sprache, Zeitzone, Helligkeit
    y = 326;
    L.karte(20, y, 440, 146, 16, C_CARD, C_LINE2);
    L.text(34, y + 31, T(T_SPRACHE), R130, C_MUTED);
    static const char *SPR[2] = {"Deutsch", "Русский"};
    for (int i = 0; i < 2; i++) {
      bool an = sprache == i; float x = 196 + i * 128;
      L.rund(x, y + 10, 122, 34, 11, an ? C_BAT : C_LINE);
      L.rund(x + 1, y + 11, 120, 32, 10, an ? C_BAT : C_CARD2);
      L.text(x + 61, y + 32, SPR[i], an ? S150 : R140, an ? C_ONBAT : C_MUTED, 1);
      knopf(x, y + 4, 122, 46, K_SPRACHE, i);
    }
    L.text(34, y + 73, T(T_ZEITZONE), R130, C_MUTED);
    L.rund(196, y + 52, 250, 34, 11, C_LINE); L.rund(197, y + 53, 248, 32, 10, C_CARD2);
    ikone(L, IK_ZURUECK, 204, y + 59, 0.85f, 2, C_MUTED2);
    { float p[6] = {426, y + 63, 432, y + 69, 426, y + 75}; L.polylinie(p, 3, 2, C_MUTED2, false); }
    L.text(321, y + 74, sprache ? ZONEN[e->zone].ru : ZONEN[e->zone].de, R140, C_TEXT, 1);
    knopf(190, y + 48, 90, 42, K_ZONE, -1); knopf(370, y + 48, 90, 42, K_ZONE, 1);
    L.text(34, y + 113, T(T_HELL_NACHT), R130, C_MUTED);
    snprintf(b, sizeof b, T(T_HELL_FMT), e->nachtHell); L.text(446, y + 113, b, R140, C_MUTED2, 2);
    L.rund(34, y + 126, 412, 6, 3, C_CARD2);
    L.rund(34, y + 126, fmaxf(6, 412 * e->nachtHell / 100.0f), 6, 3, C_BAT);
    knopf(24, y + 98, 432, 48, K_REGLER);
  }
  void feld(Leinwand &L, float x, float y, float w, const char *t, uint8_t id, int arg, bool rechts) {
    L.rund(x, y, w, 38, 12, C_LINE); L.rund(x + 1, y + 1, w - 2, 36, 11, C_BG);
    L.text(rechts ? x + w - 14 : x + 14, y + 25, t, R160, C_TEXT, rechts ? 2 : 0, true);
    knopf(x, y, w, 38, id, arg);
  }

  // ===================== WLAN wählen =====================
  void balken(Leinwand &L, float x, float yu, int rssi) {
    int lvl = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -75 ? 2 : 1;
    for (int i = 0; i < 4; i++) { float h = 6 + 4 * i; L.rund(x + i * 5, yu - h, 3, h, 1, i < lvl ? C_BAT : C_LINE3); }
  }
  void netze(Leinwand &L, const Lage &g) {
    char unter[64], s1[32], s2[40];
    snprintf(s1, sizeof s1, T(T_SCHRITT), 1);
    if (g.scanLaeuft) snprintf(unter, sizeof unter, "%s · %s", s1, T(T_SUCHE_NETZE));
    else { snprintf(s2, sizeof s2, T(T_NETZE_GEFUNDEN), g.netzAnz); snprintf(unter, sizeof unter, "%s · %s", s1, s2); }
    titel(L, T(T_WLAN_WAEHLEN), unter, !einrichtung);
    int start = netzSeite * 4;
    if (start >= g.netzAnz) { netzSeite = 0; start = 0; }
    for (int i = 0; i < 4 && start + i < g.netzAnz; i++) {
      const Netz &n = g.netze[start + i];
      float y = 88 + i * 66;
      L.karte(20, y, 440, 56, 16, C_CARD, C_LINE2);
      L.text(36, y + 34, n.ssid, R160, C_TEXT);
      if (n.offen) L.text(390, y + 34, T(T_OFFEN), R120, C_DIM, 2);
      balken(L, 413, y + 37, n.rssi);
      knopf(20, y, 440, 56, K_NETZ, start + i);
    }
    bool mehr = g.netzAnz > 4;
    taste(L, 20, 372, 215, 52, T(mehr ? T_WEITERE : T_NEU_SUCHEN), R150, C_CARD, C_MUTED2, mehr ? K_WEITERE : K_NEU_NETZE, 0);
    taste(L, 245, 372, 215, 52, T(T_NAME_EINTIPPEN), R150, C_CARD, C_MUTED2, K_NAME, 0);
    L.text(240, 456, T(T_NUR24), R120, C_DIM, 1);
  }

  // ===================== Tastatur =====================
  void tastatur(Leinwand &L, const Lage &g) {
    char unter[80], s1[32];
    snprintf(s1, sizeof s1, T(T_SCHRITT), tastZweck == 1 ? 1 : 2);
    if (tastZweck == 0) snprintf(unter, sizeof unter, "%s · %s", s1, zielSsid); else snprintf(unter, sizeof unter, "%s", s1);
    ikone(L, IK_ZURUECK, 12, 14, 1.1f, 2.2f, C_MUTED2); knopf(0, 0, 70, 56, K_ZURUECK);
    L.text(46, 37, T(tastZweck == 0 ? T_PASSWORT : T_NETZNAME), S200, C_TEXT);
    L.text(46, 58, unter, R130, C_MUTED);
    // Eingabefeld
    L.rund(20, 72, 440, 50, 14, C_BAT); L.rund(21, 73, 438, 48, 13, C_CARD);
    char sicht[80]; int n = (int)strlen(eingabe);
    if (zeigen || tastZweck == 1) snprintf(sicht, sizeof sicht, "%s", eingabe);
    else { int k = 0; for (int i = 0; i < n && k < 75; i++) { sicht[k++] = '\xE2'; sicht[k++] = '\x80'; sicht[k++] = '\xA2'; } sicht[k] = 0; }
    // Zu lange Eingaben vorne kürzen, damit das Ende sichtbar bleibt
    const char *anz = sicht;
    while (Leinwand::breite(anz, R190) > 400 && *anz) { anz++; while ((*anz & 0xC0) == 0x80) anz++; }
    float tb = L.text(36, 104, anz, R190, C_TEXT);
    L.rund(38 + tb, 85, 2, 24, 1, C_BAT);
    // Zeichen zeigen
    if (tastZweck == 0) {
      L.rund(20, 132, 18, 18, 4, zeigen ? C_BAT : C_LINE3);
      if (zeigen) ikone(L, IK_HAKEN, 21, 133, 0.67f, 2, C_ONBAT);
      L.text(46, 146, T(T_ZEICHEN_ZEIGEN), R130, C_MUTED2);
      knopf(10, 124, 220, 34, K_ZEIGEN);
    }
    char c[24]; snprintf(c, sizeof c, T(T_ZEICHEN_N), n); L.text(460, 146, c, R125, C_DIM, 2);
    // Tasten wie am Android-Handy
    static const char *KLEIN[3] = {"qwertzuiop", "asdfghjkl", "yxcvbnm"};
    static const char *ZEI1[3] = {"1234567890", "@#$_&-+()", "*\"':;!?"};
    static const char *ZEI2[3] = {"~`|^%={}[]", "<>\\/.,°€'", "\xC2\xB7\xE2\x80\xA6\xE2\x80\xA2\xC2\xB2?!-"};
    const char **r = tastModus >= 2 ? (tastModus == 2 ? ZEI1 : ZEI2) : KLEIN;
    float y = 166;
    auto reihe = [&](const char *s, float x, float w) {
      const char *p = s; int nr = 0;
      while (*p) {
        const char *a = p; uint32_t cp = Leinwand::naechstes(p);
        char t[8]; int l = (int)(p - a); memcpy(t, a, l); t[l] = 0;
        if (tastModus == 1 && cp >= 'a' && cp <= 'z') { t[0] -= 32; cp -= 32; }
        L.rund(x, y, w, 48, 9, C_TASTE);
        L.text(x + w / 2, y + 32, t, R200, C_TEXT, 1);
        // Ziffernhinweis in der ersten Reihe, wie am Handy
        if (tastModus < 2 && s == KLEIN[0]) { char z[2] = {"1234567890"[nr], 0}; L.text(x + w - 6, y + 12, z, R105, C_DIM, 2); }
        knopf(x, y - 3, w, 54, K_TASTE, (int)cp);
        x += w + 5; nr++;
      }
    };
    float w10 = (464 - 9 * 5) / 10.0f;
    reihe(r[0], 8, w10); y += 54;
    float w9 = (464 - 48 - 8 * 5) / 9.0f;
    reihe(r[1], 8 + 24, w9); y += 54;
    float u = (464 - 8 * 5) / 10.0f;
    // Umschalten
    L.rund(8, y, u * 1.5f, 48, 9, tastModus == 1 ? C_TEXT : C_SONDER);
    if (tastModus < 2) ikone(L, IK_UMSCHALT, 8 + u * 0.75f - 10, y + 14, 20 / 24.0f, 0, tastModus == 1 ? C_BG : C_MUTED2, true);
    else L.text(8 + u * 0.75f, y + 30, tastModus == 2 ? "=\\<" : "?123", R140, C_MUTED2, 1);
    knopf(8, y - 3, u * 1.5f, 54, K_TASTE, tastModus < 2 ? 1 : 5);
    reihe(r[2], 8 + u * 1.5f + 5, u);
    float xl = 8 + u * 1.5f + 5 + 7 * (u + 5);
    L.rund(xl, y, 464 + 8 - xl, 48, 9, C_SONDER);
    ikone(L, IK_LOESCHEN, xl + (464 + 8 - xl) / 2 - 11, y + 13, 22 / 24.0f, 2 * 22 / 24.0f, C_MUTED2);
    knopf(xl, y - 3, 464 + 8 - xl, 54, K_TASTE, 2);
    y += 54;
    float u4 = (464 - 4 * 5) / 10.0f, x = 8;
    taste(L, x, y, u4 * 1.5f, 48, tastModus >= 2 ? "ABC" : "?123", R140, C_SONDER, C_MUTED2, K_TASTE, 3); x += u4 * 1.5f + 5;
    taste(L, x, y, u4, 48, ",", R180, C_TASTE, C_TEXT, K_TASTE, ','); x += u4 + 5;
    taste(L, x, y, u4 * 5, 48, T(T_LEERTASTE), R135, C_SONDER, C_MUTED, K_TASTE, ' '); x += u4 * 5 + 5;
    taste(L, x, y, u4, 48, ".", R180, C_TASTE, C_TEXT, K_TASTE, '.'); x += u4 + 5;
    L.rund(x, y, u4 * 1.5f, 48, 9, C_SONDER);
    ikone(L, IK_EINGABE, x + u4 * 0.75f - 10, y + 14, 20 / 24.0f, 2 * 20 / 24.0f, C_MUTED2);
    knopf(x, y, u4 * 1.5f, 48, K_TASTE, 4);
    // Verbinden / Weiter
    taste(L, 20, 432, 440, 44, T(tastZweck == 0 ? T_VERBINDEN : T_WEITER), S160, C_BAT, C_ONBAT, K_VERBINDEN, 0);
    (void)g;
  }
  void tippeTaste(int code) {
    int n = (int)strlen(eingabe);
    if (code == 1) { tastModus = tastModus == 1 ? 0 : 1; return; }
    if (code == 5) { tastModus = tastModus == 2 ? 3 : 2; return; }
    if (code == 3) { tastModus = tastModus >= 2 ? 0 : 2; return; }
    if (code == 2) { if (n) { n--; while (n && (eingabe[n] & 0xC0) == 0x80) n--; eingabe[n] = 0; } return; }
    if (code == 4) { bestaetige(); return; }
    char t[4]; int l = 0;
    if (code < 0x80) t[l++] = (char)code;
    else if (code < 0x800) { t[l++] = (char)(0xC0 | (code >> 6)); t[l++] = (char)(0x80 | (code & 63)); }
    else { t[l++] = (char)(0xE0 | (code >> 12)); t[l++] = (char)(0x80 | ((code >> 6) & 63)); t[l++] = (char)(0x80 | (code & 63)); }
    if (n + l < (tastZweck == 0 ? 64 : 32)) { memcpy(eingabe + n, t, l); eingabe[n + l] = 0; }
    if (tastModus == 1) tastModus = 0;   // Großschreibung gilt für einen Buchstaben
  }
  void bestaetige() {
    if (tastZweck == 1) {
      if (!eingabe[0]) return;
      snprintf(zielSsid, sizeof zielSsid, "%s", eingabe);
      eingabe[0] = 0; tastZweck = 0; tastModus = 0; neu = true; return;
    }
    snprintf(e->ssid, sizeof e->ssid, "%s", zielSsid);
    snprintf(e->pass, sizeof e->pass, "%s", eingabe);
    char t[96]; snprintf(t, sizeof t, T(T_VERBINDE), zielSsid);
    zeigeMeldung(t, false);
    wuensche |= W_VERBINDEN;
  }

  // ===================== Speicher suchen =====================
  void speicher(Leinwand &L, const Lage &g) {
    char unter[64], s1[32], s2[32];
    snprintf(s1, sizeof s1, T(T_SCHRITT), 3);
    if (g.sucheLaeuft) snprintf(unter, sizeof unter, "%s · %s", s1, T(T_SUCHE_GERAETE));
    else { snprintf(s2, sizeof s2, T(T_N_GEFUNDEN), g.fundAnz); snprintf(unter, sizeof unter, "%s · %s", s1, s2); }
    titel(L, T(T_SPEICHER_SUCHEN), unter, !einrichtung);
    for (int i = 0; i < g.fundAnz && i < 4; i++) {
      const Fund &fd = g.funde[i];
      float y = 88 + i * 66;
      L.karte(20, y, 440, 56, 16, C_CARD, C_LINE2);
      char name[40]; produktName(fd.produkt, name, sizeof name);
      L.text(36, y + 34, fd.produkt[0] ? name : fd.host, R160, C_TEXT);
      L.text(444, y + 34, fd.ip, R140, C_MUTED2, 2);
      knopf(20, y, 440, 56, K_GERAET, i);
    }
    if (!g.sucheLaeuft && g.fundAnz == 0) L.text(240, 220, T(T_KEIN_GERAET), R150, C_MUTED, 1);
    taste(L, 20, 372, 215, 52, T(T_NEU_SUCHEN), R150, C_CARD, C_MUTED2, K_NEU_SUCHEN, 0);
    taste(L, 245, 372, 215, 52, T(T_IP_EINTIPPEN), R150, C_CARD, C_MUTED2, K_IP, 0);
  }

  // ===================== Zahleneingabe =====================
  void zahl(Leinwand &L, const Lage &g) {
    char t[48];
    if (zahlFeld < 4) snprintf(t, sizeof t, T(T_MP_PV), zahlFeld + 1);
    else snprintf(t, sizeof t, "%s", T(zahlFeld == 4 ? T_KAPAZITAET : T_IP_ADRESSE));
    ikone(L, IK_ZURUECK, 12, 14, 1.1f, 2.2f, C_MUTED2); knopf(0, 0, 70, 56, K_ZURUECK);
    L.text(46, 37, t, S200, C_TEXT);
    L.rund(20, 60, 440, 54, 14, C_BAT); L.rund(21, 61, 438, 52, 13, C_CARD);
    float tb = L.text(440, 96, eingabe, S220, C_TEXT, 2, true);
    (void)tb;
    static const char *Z = "123456789";
    float kw = (440 - 2 * 10) / 3.0f, kh = 58;
    for (int i = 0; i < 9; i++) {
      char s[2] = {Z[i], 0};
      taste(L, 20 + (i % 3) * (kw + 10), 126 + (i / 3) * (kh + 8), kw, kh, s, S220, C_TASTE, C_TEXT, K_ZAHL, Z[i]);
    }
    float y = 126 + 3 * (kh + 8);
    if (zahlFeld == 5) taste(L, 20, y, kw, kh, ".", S220, C_TASTE, C_TEXT, K_ZAHL, '.');
    taste(L, 20 + kw + 10, y, kw, kh, "0", S220, C_TASTE, C_TEXT, K_ZAHL, '0');
    L.rund(20 + 2 * (kw + 10), y, kw, kh, 11, C_SONDER);
    ikone(L, IK_LOESCHEN, 20 + 2 * (kw + 10) + kw / 2 - 13, y + kh / 2 - 13, 26 / 24.0f, 2.2f, C_MUTED2);
    knopf(20 + 2 * (kw + 10), y, kw, kh, K_ZAHL, 8);
    taste(L, 20, 412, 440, 52, T(T_UEBERNEHMEN), S160, C_BAT, C_ONBAT, K_OK, 0);
    (void)g;
  }

  // ===================== Meldung =====================
  void zeigeMeldung(const char *t, bool fehler) { snprintf(meldung, sizeof meldung, "%s", t); meldungFehler = fehler; gehe(S_MELDUNG); }
  void meldungSeite(Leinwand &L, const Lage &g) {
    // Zeilenumbruch an Leerzeichen, höchstens 420 px breit
    char zeile[96]; const char *p = meldung; float y = meldungFehler ? 200 : 236;
    while (*p) {
      int best = 0, i = 0;
      while (p[i]) {
        if (p[i] == ' ' || !p[i + 1]) {
          int l = p[i] == ' ' ? i : i + 1;
          memcpy(zeile, p, l); zeile[l] = 0;
          if (Leinwand::breite(zeile, R180) > 420 && best) break;
          best = l;
        }
        i++;
      }
      if (!best) best = (int)strlen(p);
      memcpy(zeile, p, best); zeile[best] = 0;
      L.text(240, y, zeile, R180, meldungFehler ? C_TEXT : C_MUTED2, 1);
      y += 28; p += best; while (*p == ' ') p++;
    }
    if (meldungFehler) {
      taste(L, 20, 380, 215, 52, T(T_ZURUECK), R150, C_CARD, C_MUTED2, K_NEU_NETZE, 0);
      taste(L, 245, 380, 215, 52, T(T_NOCHMAL), S150, C_BAT, C_ONBAT, K_NOCHMAL, 0);
    }
    (void)g;
  }


  // ===================== Firmware-Update =====================
  void update(Leinwand &L, const Lage &g) {
    bool laeuft = g.updStatus == 2 || g.updStatus == 3;
    if (!laeuft) { ikone(L, IK_ZURUECK, 12, 14, 1.1f, 2.2f, C_MUTED2); knopf(0, 0, 230, 56, K_ZURUECK); }
    L.text(46, 37, T(T_FIRMWARE), S200, C_TEXT);
    char b[64];
    L.karte(20, 64, 440, 118, 16, C_CARD, C_LINE2);
    L.text(34, 96, T(T_INSTALLIERT), R140, C_MUTED);
    snprintf(b, sizeof b, "Build %d", g.build); L.text(446, 96, b, S160, C_TEXT, 2, true);
    L.flaeche(34, 122, 412, 1, C_LINE);
    L.text(34, 156, T(T_VERFUEGBAR), R140, C_MUTED);
    if (g.updStatus == 1) snprintf(b, sizeof b, "%s", T(T_PRUEFE));
    else if (!g.updGeprueft) snprintf(b, sizeof b, "%s", T(T_NICHT_GEPRUEFT));
    else if (g.updBuild > g.build) snprintf(b, sizeof b, "Build %d", g.updBuild);
    else snprintf(b, sizeof b, "%s", T(T_AKTUELL));
    L.text(446, 156, b, S160, g.updBuild > g.build ? C_BAT : C_MUTED2, 2, true);
    // Hinweis, in Zeilen umbrochen
    zeilen(L, T(T_UPD_HINWEIS), 34, 222, 412, R140, C_MUTED2);
    if (g.updStatus == 2 || g.updStatus == 3) {
      if (g.updStatus == 2) snprintf(b, sizeof b, T(T_LADE), g.updBuild, g.updProzent);
      else snprintf(b, sizeof b, "%s", T(T_NEUSTART));
      L.text(240, 330, b, R160, C_TEXT, 1, true);
      L.rund(34, 346, 412, 8, 4, C_CARD2);
      L.rund(34, 346, fmaxf(8, 412 * g.updProzent / 100.0f), 8, 4, C_BAT);
      return;
    }
    if (g.updStatus == 4) {
      snprintf(b, sizeof b, "%s", T(T_UPD_FEHLER)); L.text(240, 318, b, S160, C_WARN, 1);
      L.text(240, 344, g.updFehler, R130, C_MUTED, 1);
    }
    bool neuer = g.updBuild > g.build && g.updStatus != 1;
    taste(L, 20, 400, neuer ? 215 : 440, 56, T(T_JETZT_PRUEFEN), R150, C_CARD, C_MUTED2, K_UPD_PRUEFEN, 0);
    if (neuer) taste(L, 245, 400, 215, 56, T(T_INSTALLIEREN), S160, C_BAT, C_ONBAT, K_UPD_INSTALL, 0);
  }
  // Text an Leerzeichen umbrechen; gibt die Höhe zurück
  float zeilen(Leinwand &L, const char *t, float x, float y, float breit, const Schrift &f, uint16_t c) {
    char z[160]; const char *p = t; float y0 = y;
    while (*p) {
      int best = 0;
      for (int i = 0; p[i]; i++) {
        if (p[i] == ' ' || !p[i + 1]) {
          int l = p[i] == ' ' ? i : i + 1; if (l >= (int)sizeof z) break;
          memcpy(z, p, l); z[l] = 0;
          if (Leinwand::breite(z, f) > breit && best) break;
          best = l;
        }
      }
      if (!best) best = (int)strlen(p) < (int)sizeof z - 1 ? (int)strlen(p) : (int)sizeof z - 1;
      memcpy(z, p, best); z[best] = 0;
      L.text(x, y, z, f, c);
      y += f.asc + f.desc + 4; p += best; while (*p == ' ') p++;
    }
    return y - y0;
  }

  // ===================== Zeichnen und Tippen =====================
  void zeichne(Leinwand &L, const Lage &g) {
    knAnz = 0; pilleR = -1;
    L.fuellen(C_BG);
    switch (seite) {
      case S_FLUSS: fluss(L, g); break;
      case S_ZUSTAND: zustand(L, g); break;
      case S_VERLAUF: verlauf(L, g); break;
      case S_EINST: einst(L, g); break;
      case S_NETZE: netze(L, g); break;
      case S_TASTATUR: tastatur(L, g); break;
      case S_SPEICHER: speicher(L, g); break;
      case S_ZAHL: zahl(L, g); break;
      case S_MELDUNG: meldungSeite(L, g); break;
      case S_UPDATE: update(L, g); break;
    }
    if (seite != S_FLUSS) drahtAnz = 0;
    neu = false;
  }
  bool hauptseite() const { return seite <= S_VERLAUF; }
  void wische(int richtung) {         // +1 nach links gewischt = nächste Seite
    if (!hauptseite()) return;
    int s = (int)seite + richtung;
    if (s < 0 || s > 2) return;
    gehe((Seite)s);
  }
  void lange() { if (hauptseite()) gehe(S_EINST); }
  void zahlStart(int feldNr, const Lage &g) {
    zahlFeld = feldNr;
    if (feldNr < 4) snprintf(eingabe, sizeof eingabe, "%d", e->peaks[feldNr]);
    else if (feldNr == 4) snprintf(eingabe, sizeof eingabe, "%d", e->cap > 0 ? e->cap : kapazitaet(g.w));
    else snprintf(eingabe, sizeof eingabe, "%s", e->ip);
    gehe(S_ZAHL);
  }
  void tippe(int x, int y, const Lage &g) {
    for (int i = knAnz - 1; i >= 0; i--) {
      const Knopf &k = kn[i];
      if (x < k.x || y < k.y || x >= k.x + k.w || y >= k.y + k.h) continue;
      switch (k.id) {
        case K_PILLE: gehe(S_EINST); break;
        case K_UPDATE: wuensche |= W_UPD_PRUEFEN; gehe(S_UPDATE); break;
        case K_UPD_PRUEFEN: wuensche |= W_UPD_PRUEFEN; neu = true; break;
        case K_UPD_INSTALL: wuensche |= W_UPD_LADEN; neu = true; break;
        case K_PUNKT: gehe((Seite)k.arg); break;
        case K_NETZ:
          snprintf(zielSsid, sizeof zielSsid, "%s", g.netze[k.arg].ssid);
          eingabe[0] = 0; tastZweck = 0; tastModus = 0;
          if (g.netze[k.arg].offen) { bestaetige(); break; }
          gehe(S_TASTATUR); break;
        case K_WEITERE: netzSeite++; neu = true; break;
        case K_NEU_NETZE: netzSeite = 0; wuensche |= W_SCAN; gehe(S_NETZE); break;
        case K_NAME: eingabe[0] = 0; tastZweck = 1; tastModus = 0; gehe(S_TASTATUR); break;
        case K_TASTE: tippeTaste(k.arg); neu = true; break;
        case K_ZEIGEN: zeigen = !zeigen; neu = true; break;
        case K_VERBINDEN: bestaetige(); neu = true; break;
        case K_NOCHMAL: eingabe[0] = 0; snprintf(eingabe, sizeof eingabe, "%s", e->pass); tastZweck = 0; gehe(S_TASTATUR); break;
        case K_ZURUECK:
          if (seite == S_EINST) gehe(S_FLUSS);
          else if (seite == S_UPDATE) gehe(S_EINST);
          else if (seite == S_TASTATUR) gehe(S_NETZE);
          else if (seite == S_ZAHL) gehe(zahlFeld == 5 ? S_SPEICHER : S_EINST);
          else gehe(einrichtung ? S_NETZE : S_EINST);
          break;
        case K_GERAET:
          snprintf(e->ip, sizeof e->ip, "%s", g.funde[k.arg].ip);
          snprintf(e->host, sizeof e->host, "%s", g.funde[k.arg].host);
          snprintf(e->produkt, sizeof e->produkt, "%s", g.funde[k.arg].produkt);
          wuensche |= W_GERAET | W_SICHERN; einrichtung = false; gehe(S_FLUSS); break;
        case K_NEU_SUCHEN: wuensche |= W_SUCHE; neu = true; break;
        case K_IP: zahlStart(5, g); break;
        case K_SP_AENDERN: wuensche |= W_SUCHE; gehe(S_SPEICHER); break;
        case K_WLAN_AENDERN: netzSeite = 0; wuensche |= W_SCAN; gehe(S_NETZE); break;
        case K_FELD: zahlStart(k.arg, g); break;
        case K_SPRACHE: sprache = e->sprache = k.arg; wuensche |= W_SICHERN; neu = true; break;
        case K_ZONE: e->zone = (e->zone + k.arg + ZONEN_ANZ) % ZONEN_ANZ; wuensche |= W_SICHERN | W_ZONE; neu = true; break;
        case K_REGLER: {
          int p = (int)lroundf((x - 34) / 412.0f * 20) * 5;
          e->nachtHell = p < 5 ? 5 : p > 100 ? 100 : p;
          wuensche |= W_SICHERN | W_HELL; neu = true; break;
        }
        case K_ZAHL: {
          int n = (int)strlen(eingabe);
          if (k.arg == 8) { if (n) eingabe[n - 1] = 0; }
          else if (n < (zahlFeld == 5 ? 15 : 5)) { eingabe[n] = (char)k.arg; eingabe[n + 1] = 0; }
          neu = true; break;
        }
        case K_OK:
          if (zahlFeld < 4) { int v = atoi(eingabe); if (v > 0 && v <= 3000) e->peaks[zahlFeld] = v; }
          else if (zahlFeld == 4) { int v = atoi(eingabe); e->cap = v >= 0 && v <= 100000 ? v : e->cap; }
          else {
            snprintf(e->ip, sizeof e->ip, "%s", eingabe); e->host[0] = 0; e->produkt[0] = 0;
            wuensche |= W_GERAET; einrichtung = false;
            wuensche |= W_SICHERN; gehe(S_FLUSS); break;
          }
          wuensche |= W_SICHERN; gehe(S_EINST); break;
      }
      return;
    }
  }
};
