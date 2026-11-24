#pragma once
// Werte aus dem lokalen Bericht des SolarFlow (GET /properties/report) und die
// Rechnungen, die die App daraus macht. Ohne Arduino-Abhängigkeit.
//
// Ein Feld, das der Bericht nicht enthält, bleibt NAN - nicht 0. So kann die
// Anzeige "nicht gemeldet" von "gemeldet: 0" unterscheiden, wie die App auch.

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "modell.h"

enum Feld {
  F_SOLAR, F_PV1, F_PV2, F_PV3, F_PV4, F_PV5, F_PV6,
  F_HOME, F_GRID, F_LADEN, F_ENTLADEN, F_SOC, F_REST, F_TEMP, F_BATVOLT,
  F_ACMODE, F_INLIMIT, F_OUTLIMIT, F_SOCSET, F_MINSOC, F_INVMAX, F_CHGMAX,
  F_RSSI, F_PACKNUM, F_PACKSTATE, F_FAULT, F_ISERR, F_HEAT, F_DERATE,
  F_OFFMODE, F_OFFPOWER, F_FANMODE, F_FANSPEED,
  F_ANZ
};
static const char *const FELD_NAMEN[F_ANZ] = {
  "solarInputPower", "solarPower1", "solarPower2", "solarPower3", "solarPower4", "solarPower5", "solarPower6",
  "outputHomePower", "gridInputPower", "outputPackPower", "packInputPower", "electricLevel", "remainOutTime",
  "hyperTmp", "BatVolt", "acMode", "inputLimit", "outputLimit", "socSet", "minSoc", "inverseMaxPower",
  "chargeMaxLimit", "rssi", "packNum", "packState", "faultLevel", "is_error", "heatState", "derateFlag",
  "gridOffMode", "gridOffPower", "Fanmode", "Fanspeed"
};
enum PackFeld { P_MAXTEMP, P_MAXVOL, P_MINVOL, P_TOTALVOL, P_BATCUR, P_POWER, P_SOFT, P_ANZ };
static const char *const PACK_NAMEN[P_ANZ] = {
  "maxTemp", "maxVol", "minVol", "totalVol", "batcur", "power", "softVersion"
};

struct Werte {
  bool da = false;
  char produkt[32] = "";
  char sn[40] = "";
  float f[F_ANZ];
  float pk[P_ANZ];
  void leeren() {
    da = false; produkt[0] = sn[0] = 0;
    for (int i = 0; i < F_ANZ; i++) f[i] = NAN;
    for (int i = 0; i < P_ANZ; i++) pk[i] = NAN;
  }
  Werte() { leeren(); }
  bool hat(int i) const { return !isnan(f[i]); }
  float n(int i) const { return isnan(f[i]) ? 0 : f[i]; }   // wie "?? 0" in der App
};

// ---------- kleiner JSON-Leser ----------
// Liest nur, was gebraucht wird: product, sn, alle Zahlen unter "properties"
// und unter dem ersten Eintrag von "packData". Alles andere wird überlesen.
struct JsonLeser {
  const char *s; Werte *w; bool fehler = false;
  enum { OBEN, EIGENSCH, PACKLISTE, PACK0, SONST };
  void leer() { while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') s++; }
  bool zeichen(char c) { leer(); if (*s == c) { s++; return true; } return false; }
  void kette(char *ziel, int max) {
    int n = 0;
    if (*s != '"') { fehler = true; return; }
    s++;
    while (*s && *s != '"') {
      char c = *s++;
      if (c == '\\' && *s) { c = *s++; if (c == 'u') { for (int i = 0; i < 4 && *s; i++) s++; c = '?'; } }
      if (ziel && n < max - 1) ziel[n++] = c;
    }
    if (*s == '"') s++; else fehler = true;
    if (ziel) ziel[n] = 0;
  }
  void wert(int ort, const char *schluessel) {
    leer();
    if (*s == '{') {
      s++;
      int innen = SONST;
      if (ort == OBEN && schluessel && !strcmp(schluessel, "properties")) innen = EIGENSCH;
      if (ort == PACKLISTE && schluessel && !strcmp(schluessel, "#0")) innen = PACK0;
      if (ort == -1) innen = OBEN;
      if (zeichen('}')) return;
      do {
        leer(); char k[32]; kette(k, sizeof k); if (fehler) return;
        if (!zeichen(':')) { fehler = true; return; }
        wert(innen, k); if (fehler) return;
      } while (zeichen(','));
      if (!zeichen('}')) fehler = true;
    } else if (*s == '[') {
      s++;
      int innen = (ort == OBEN && schluessel && !strcmp(schluessel, "packData")) ? PACKLISTE : SONST;
      if (zeichen(']')) return;
      int i = 0;
      do { wert(innen, i == 0 ? "#0" : "#n"); i++; if (fehler) return; } while (zeichen(','));
      if (!zeichen(']')) fehler = true;
    } else if (*s == '"') {
      if (ort == OBEN && !strcmp(schluessel, "product")) kette(w->produkt, sizeof w->produkt);
      else if (ort == OBEN && !strcmp(schluessel, "sn")) kette(w->sn, sizeof w->sn);
      else kette(nullptr, 0);
    } else {
      float v = NAN;
      if (!strncmp(s, "true", 4)) { v = 1; s += 4; }
      else if (!strncmp(s, "false", 5)) { v = 0; s += 5; }
      else if (!strncmp(s, "null", 4)) { s += 4; }
      else { char *e; v = strtof(s, &e); if (e == s) { fehler = true; return; } s = e; }
      if (ort == EIGENSCH) {
        for (int i = 0; i < F_ANZ; i++) if (!strcmp(schluessel, FELD_NAMEN[i])) { w->f[i] = v; break; }
      } else if (ort == PACK0) {
        for (int i = 0; i < P_ANZ; i++) if (!strcmp(schluessel, PACK_NAMEN[i])) { w->pk[i] = v; break; }
      }
    }
  }
};
// true, wenn der Text ein Bericht eines Speichers war (mit "properties").
static bool leseBericht(const char *text, Werte &w) {
  w.leeren();
  JsonLeser j; j.s = text; j.w = &w;
  j.wert(-1, nullptr);
  bool eigensch = false;
  for (int i = 0; i < F_ANZ; i++) if (w.hat(i)) eigensch = true;
  w.da = !j.fehler && eigensch;
  return w.da;
}

// "solarFlow800Pro2" -> "SolarFlow 800 Pro 2", wie prodName() in der App
static void produktName(const char *p, char *aus, int max) {
  if (!p || !*p) { snprintf(aus, max, "SolarFlow"); return; }
  int n = 0, i = 0;
  if (!strncmp(p, "solarFlow", 9) || !strncmp(p, "SolarFlow", 9)) {      // "SolarFlow" bleibt ein Wort
    memcpy(aus, "SolarFlow", 9); n = 9; i = 9;
    if (p[i]) aus[n++] = ' ';
  }
  for (; p[i] && n < max - 2; i++) {
    char c = p[i], v = i ? p[i - 1] : 0;
    if (i == 0 && c >= 'a' && c <= 'z') c -= 32;
    bool zahl = c >= '0' && c <= '9', vzahl = v >= '0' && v <= '9';
    bool gross = c >= 'A' && c <= 'Z', vklein = v >= 'a' && v <= 'z';
    bool vbuch = (v >= 'a' && v <= 'z') || (v >= 'A' && v <= 'Z');
    bool buch = gross || (c >= 'a' && c <= 'z');
    if (i > 0 && n > 0 && aus[n - 1] != ' ' && ((zahl && vbuch) || (buch && vzahl) || (gross && vklein))) aus[n++] = ' ';
    aus[n++] = c;
  }
  aus[n] = 0;
}

static inline float tempC(float roh) {
  return (isnan(roh) || roh == 0) ? NAN : (roh - UM_TEMPOFFSETZEHNTELKELVIN) / (float)UM_TEMPTEILER;
}

// Alles, was das Flussdiagramm zeigt - dieselben Rechnungen wie render() in der App.
struct Fluss {
  float solar, home, grid, laden, entladen, soc;
  float verlust;            // NAN = nichts anzeigen
  int pvAnz;                // 0 = AC-gekoppelt, kein Solarzweig
  float pv[6];              // NAN = Strich statt Zahl
  bool ohnePv;
  float batW; int batZustand;   // 0 ruht, 1 lädt, 2 entlädt
  bool hatOff; float offW; bool offBereit;
  float minSoc, maxSoc;
  int restMin;              // -1 = keine Angabe
};
static Fluss rechneFluss(const Werte &w) {
  Fluss f;
  f.solar = w.n(F_SOLAR); f.home = w.n(F_HOME); f.grid = w.n(F_GRID);
  f.laden = w.n(F_LADEN); f.entladen = w.n(F_ENTLADEN); f.soc = w.n(F_SOC);
  // Was im Gerät bleibt: hinein minus heraus. Unter 15 W Zufluss nur Rauschen.
  float rein = f.solar + f.entladen + f.grid, raus = f.home + f.laden;
  f.verlust = (rein >= 15 && rein - raus >= 1) ? roundf(rein - raus) : NAN;
  int letzter = -1;
  for (int i = 0; i < 6; i++) if (w.hat(F_PV1 + i)) letzter = i;
  f.ohnePv = letzter < 0 && !w.hat(F_SOLAR);
  f.pvAnz = f.ohnePv ? 0 : (letzter + 1 < 2 ? 2 : letzter + 1);
  float summe = 0; bool eins = false;
  for (int i = 0; i < f.pvAnz; i++) { summe += w.n(F_PV1 + i); if (w.hat(F_PV1 + i)) eins = true; }
  // Unter etwa 10 W je Eingang meldet das Gerät nur die Summe: Striche statt Nullen.
  bool stumm = eins && summe == 0 && f.solar > 2;
  for (int i = 0; i < 6; i++) f.pv[i] = (stumm || !w.hat(F_PV1 + i)) ? NAN : w.f[F_PV1 + i];
  f.batW = 0; f.batZustand = 0;
  if (f.laden > 0) { f.batW = f.laden; f.batZustand = 1; }
  else if (f.entladen > 0) { f.batW = f.entladen; f.batZustand = 2; }
  f.hatOff = w.hat(F_OFFPOWER) || w.hat(F_OFFMODE);
  f.offW = w.n(F_OFFPOWER); f.offBereit = w.n(F_OFFMODE) != 0;
  f.minSoc = (w.hat(F_MINSOC) ? w.f[F_MINSOC] : 100) / 10;
  f.maxSoc = (w.hat(F_SOCSET) ? w.f[F_SOCSET] : 1000) / 10;
  f.restMin = (f.entladen > 0 && w.n(F_REST) > 0 && w.n(F_REST) < 59000) ? (int)w.f[F_REST] : -1;
  return f;
}
