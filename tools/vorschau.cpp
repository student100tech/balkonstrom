// Rendert alle Monitor-Seiten am PC als Bilder - mit genau dem Zeichencode,
// der auch auf dem Board läuft.
//   g++ -O2 -std=c++17 -I firmware/pvmonitor tools/vorschau.cpp -o /tmp/vorschau && /tmp/vorschau /tmp/vorschau
#include <cstdio>
#include <cstdlib>
#include <string>
#include "seiten.h"

static uint16_t bild[480 * 480];

static void sichern(const std::string &pfad) {
  FILE *f = fopen(pfad.c_str(), "wb");
  fprintf(f, "P6\n480 480\n255\n");
  for (int y = 0; y < 480; y++) for (int x = 0; x < 480; x++) {
    uint16_t c = bild[(479 - x) * 480 + y];            // Panel-Lage zurück ins Bild
    uint8_t r = (c >> 11) << 3, g = ((c >> 5) & 63) << 2, b = (c & 31) << 3;
    r |= r >> 5; g |= g >> 6; b |= b >> 5;
    fputc(r, f); fputc(g, f); fputc(b, f);
  }
  fclose(f);
}

// Bericht wie Szene "alles" der App-Demo (800 Pro 2, zwei Packs, Off-Grid-Dose)
static const char *PRO2 = R"({"timestamp":1764000000,"messageId":1,"sn":"HOA1NAN9N123456","product":"solarFlow800Pro2",
 "properties":{"solarInputPower":690,"solarPower1":210,"solarPower2":180,"solarPower3":160,"solarPower4":140,
 "outputHomePower":800,"packInputPower":180,"outputPackPower":0,"gridInputPower":0,"electricLevel":64,
 "remainOutTime":320,"hyperTmp":3121,"BatVolt":5120,"acMode":2,"inputLimit":0,"outputLimit":800,"socSet":1000,
 "minSoc":150,"inverseMaxPower":800,"chargeMaxLimit":1200,"rssi":-58,"packNum":2,"packState":2,"faultLevel":1,
 "is_error":0,"heatState":0,"derateFlag":0,"gridOffMode":1,"gridOffPower":120,"Fanmode":1,"Fanspeed":2},
 "packData":[{"sn":"x","maxTemp":2891,"maxVol":331,"minVol":329,"totalVol":4965,"batcur":65509,"power":180,"socLevel":64,"softVersion":4354,"state":1},
 {"maxTemp":2884,"maxVol":331,"minVol":329,"totalVol":4962,"batcur":65512,"power":180,"socLevel":64}]})";
// 800 Plus mittags: lädt mit Sonne, Netzladen aus
static const char *PLUS = R"({"sn":"P1","product":"solarFlow800Plus","properties":{"solarInputPower":1210,
 "solarPower1":620,"solarPower2":590,"outputHomePower":430,"packInputPower":0,"outputPackPower":760,
 "gridInputPower":0,"electricLevel":78,"hyperTmp":3221,"acMode":2,"outputLimit":430,"socSet":1000,"minSoc":100,
 "inverseMaxPower":800,"chargeMaxLimit":1000,"rssi":-71,"packNum":1,"heatState":0},
 "packData":[{"maxTemp":2981,"maxVol":334,"minVol":330,"totalVol":5000,"batcur":152,"power":760}]})";

int main(int argc, char **argv) {
  std::string ziel = argc > 1 ? argv[1] : "/tmp/vorschau";
  static Einst e; static Lage g; static Verlauf v; static Oberflaeche o;
  e.peaks[0] = 450; e.peaks[1] = 450; e.peaks[2] = 400; e.peaks[3] = 400;
  snprintf(e.ssid, sizeof e.ssid, "FRITZ!Box 7590 OG"); snprintf(e.ip, sizeof e.ip, "192.168.178.52");
  snprintf(e.produkt, sizeof e.produkt, "solarFlow800Pro2");
  o.e = &e;
  g.uhr = true; g.hh = 18; g.mm = 54; g.ss = 12; g.alterS = 2; g.build = 8;
  if (!leseBericht(PRO2, g.w)) { printf("Bericht nicht gelesen!\n"); return 1; }
  // Verlauf: ein Tag mit Sonne und Abendverbrauch, wie im Entwurf
  v.leeren(20261124);
  for (int i = 0; i < 18 * 12 + 11; i++) {
    float h = i / 12.0f, sonne = fmaxf(0, sinf((h - 8.2f) / 8.6f * (float)M_PI));
    v.solar[i] = (int16_t)(820 * sonne);
    v.haus[i] = (int16_t)(180 + 40 * sinf(i * 0.7f) + (h > 6.9f && h < 7.6f ? 520 : 0) + (h > 11.5f && h < 12.2f ? 1050 : 0) + (h > 17.8f ? 760 : 0));
    v.laden[i] = (int16_t)fmaxf(0, v.solar[i] - v.haus[i]);
    v.whSolar += v.solar[i] / 12.0f; v.whHaus += v.haus[i] / 12.0f;
  }
  v.whNetz = 380;
  g.verlauf = &v;
  // Netze und Funde für die Einrichtung
  const char *ss[] = {"FRITZ!Box 7590 OG", "FRITZ!Box 7590 Keller", "Balkonstrom-Gast", "Telekom-5A21C9", "Nachbar", "Vodafone-1234"};
  int rs[] = {-48, -62, -70, -80, -85, -88};
  for (int i = 0; i < 6; i++) { snprintf(g.netze[i].ssid, 33, "%s", ss[i]); g.netze[i].rssi = rs[i]; g.netze[i].offen = i == 2; }
  g.netzAnz = 6;
  g.fundAnz = 2;
  snprintf(g.funde[0].host, 48, "Zendure-SolarFlow800Pro2-A1B2C3D4E5F6"); snprintf(g.funde[0].ip, 16, "192.168.178.52");
  snprintf(g.funde[0].produkt, 32, "solarFlow800Pro2");
  snprintf(g.funde[1].host, 48, "Zendure-SolarFlow800Plus-112233445566"); snprintf(g.funde[1].ip, 16, "192.168.178.61");
  snprintf(g.funde[1].produkt, 32, "solarFlow800Plus");

  Leinwand L; L.init(bild, 0, 0, 480, 480);
  struct { const char *name; Seite s; int spr; } liste[] = {
    {"fluss", S_FLUSS, 0}, {"zustand", S_ZUSTAND, 0}, {"verlauf", S_VERLAUF, 0}, {"einst", S_EINST, 0},
    {"netze", S_NETZE, 0}, {"tastatur", S_TASTATUR, 0}, {"speicher", S_SPEICHER, 0}, {"zahl", S_ZAHL, 0},
    {"fluss_ru", S_FLUSS, 1}, {"zustand_ru", S_ZUSTAND, 1}, {"verlauf_ru", S_VERLAUF, 1}, {"einst_ru", S_EINST, 1},
  };
  for (auto &l : liste) {
    sprache = l.spr; o.seite = l.s;
    if (l.s == S_TASTATUR) { snprintf(o.eingabe, sizeof o.eingabe, "Sonnenschein42!"); snprintf(o.zielSsid, 33, "FRITZ!Box 7590 OG"); }
    if (l.s == S_ZAHL) { o.zahlFeld = 0; snprintf(o.eingabe, sizeof o.eingabe, "450"); }
    o.zeichne(L, g);
    // Striche zu einem festen Zeitpunkt dazu, wie sie auf dem Board laufen
    for (int i = 0; i < o.drahtAnz; i++) Oberflaeche::striche(L, o.draht[i], 400);
    sichern(ziel + "_" + l.name + ".ppm");
  }
  // 800 Plus
  sprache = 0; leseBericht(PLUS, g.w); o.seite = S_FLUSS; o.zeichne(L, g);
  for (int i = 0; i < o.drahtAnz; i++) Oberflaeche::striche(L, o.draht[i], 400);
  sichern(ziel + "_fluss_plus.ppm");
  o.seite = S_ZUSTAND; o.zeichne(L, g); sichern(ziel + "_zustand_plus.ppm");
  o.seite = S_FLUSS; g.w.leeren(); g.alterS = -1; o.zeichne(L, g); sichern(ziel + "_fluss_leer.ppm");
  g.updBuild = 11; g.updGeprueft = true; o.seite = S_EINST; o.zeichne(L, g); sichern(ziel + "_einst_upd.ppm");
  o.seite = S_UPDATE; o.zeichne(L, g); sichern(ziel + "_update.ppm");
  g.updStatus = 2; g.updProzent = 45; o.zeichne(L, g); sichern(ziel + "_update_laedt.ppm");
  g.updStatus = 0;
  o.zeigeMeldung("Verbindung fehlgeschlagen. Bitte das Passwort prüfen.", true); o.zeichne(L, g); sichern(ziel + "_meldung.ppm");
  printf("fertig\n");
  return 0;
}
