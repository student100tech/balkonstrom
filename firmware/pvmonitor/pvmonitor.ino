// PV-Monitor - Firmware für das Wandbild (Guition ESP32-S3-4848S040, 480 x 480)
//
// Liest den Speicher im WLAN über dessen lokale Schnittstelle
// (GET http://<IP>/properties/report) und zeigt dieselben Seiten wie die App:
// Energiefluss, Zustand und den Tagesverlauf, den der Monitor selbst mitschreibt.
// Nur lesen - der Monitor schreibt nichts an den Speicher.
//
// Aufteilung:
//   Kern 0  Netz-Task: WLAN, Suche nach Netzen und Speichern, Abfrage alle 3 s,
//           Uhrzeit, Verlauf mitschreiben
//   Kern 1  loop(): Touch, Zeichnen, laufende Striche
// Das Zeichnen selbst steckt in seiten.h und leinwand.h und hat keine
// Arduino-Abhängigkeit; tools/vorschau.cpp rendert damit dieselben Seiten am PC.
//
// Einbaulage: Der Halter dreht das Board um 90 Grad im Uhrzeigersinn, die
// USB-Buchse zeigt nach unten. Die Leinwand liegt deshalb im Speicher schon in
// der Lage des Panels (siehe leinwand.h), der Touch wird zurückgerechnet.

#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <time.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include "esp_ota_ops.h"
#include "lwip/sockets.h"
#include "seiten.h"
#include "zertifikate.h"
#include "plz.h"

#ifndef FW_BUILD
#define FW_BUILD 0
#endif

static const int PIN_LICHT = 38;        // Hintergrundlicht, PWM
static const int PIN_SDA   = 19;        // I2C zum Touch GT911
static const int PIN_SCL   = 45;
static const uint8_t GT911_ADDRS[2] = {0x5D, 0x14};

// ---------- Panel wie im Herstellerdemo (Build 6/7 auf dem Board bestätigt) ----------
Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
  39 /* CS */, 48 /* SCK */, 47 /* SDA */,
  18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
  11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */,  0 /* R4 */,
   8 /* G0 */, 20 /* G1 */,  3 /* G2 */, 46 /* G3 */,  9 /* G4 */, 10 /* G5 */,
   4 /* B0 */,  5 /* B1 */,  6 /* B2 */,  7 /* B3 */, 15 /* B4 */);
// Startfolge des ST7701 wortgleich aus der Bibliothekskopie im Herstellerpaket
// (1-Demo/Demo_Arduino/Libraries/Arduino_GFX-master). Die gleichnamige Fassung
// 1.2.9 aus dem Bibliotheksverwalter ist nicht dieselbe: Der Hersteller hat
// COLCTRL (0xCD) auf 0x00 gesetzt (sein Kommentar nennt das Original 0x08) und "Umkehr an" (0x21)
// herausgenommen. 0xCD legt fest, wie die 16 Datenleitungen auf die Farbbits
// des Panels verteilt werden. Mit 0x08 kamen reine Farben richtig an, Mischtöne
// aber verdreht - der fast schwarze Grund wurde orange (Build 9). Die
// Farbumkehr braucht das Panel trotzdem, siehe setup().
static const uint8_t herstellerInit[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,

    WRITE_C8_D16, 0xC0, 0x3B, 0x00,
    WRITE_C8_D16, 0xC1, 0x0D, 0x02,
    WRITE_C8_D16, 0xC2, 0x31, 0x05,
    WRITE_C8_D8, 0xCD, 0x00,//0x08

    WRITE_COMMAND_8, 0xB0, // Positive Voltage Gamma Control
    WRITE_BYTES, 16,
    0x00, 0x11, 0x18, 0x0E,
    0x11, 0x06, 0x07, 0x08,
    0x07, 0x22, 0x04, 0x12,
    0x0F, 0xAA, 0x31, 0x18,

    WRITE_COMMAND_8, 0xB1, // Negative Voltage Gamma Control
    WRITE_BYTES, 16,
    0x00, 0x11, 0x19, 0x0E,
    0x12, 0x07, 0x08, 0x08,
    0x08, 0x22, 0x04, 0x11,
    0x11, 0xA9, 0x32, 0x18,

    // PAGE1
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x11,

    WRITE_C8_D8, 0xB0, 0x60, // Vop=4.7375v
    WRITE_C8_D8, 0xB1, 0x32, // VCOM=32
    WRITE_C8_D8, 0xB2, 0x07, // VGH=15v
    WRITE_C8_D8, 0xB3, 0x80,
    WRITE_C8_D8, 0xB5, 0x49, // VGL=-10.17v
    WRITE_C8_D8, 0xB7, 0x85,
    WRITE_C8_D8, 0xB8, 0x21, // AVDD=6.6 & AVCL=-4.6
    WRITE_C8_D8, 0xC1, 0x78,
    WRITE_C8_D8, 0xC2, 0x78,

    WRITE_COMMAND_8, 0xE0,
    WRITE_BYTES, 3, 0x00, 0x1B, 0x02,

    WRITE_COMMAND_8, 0xE1,
    WRITE_BYTES, 11,
    0x08, 0xA0, 0x00, 0x00,
    0x07, 0xA0, 0x00, 0x00,
    0x00, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE2,
    WRITE_BYTES, 12,
    0x11, 0x11, 0x44, 0x44,
    0xED, 0xA0, 0x00, 0x00,
    0xEC, 0xA0, 0x00, 0x00,

    WRITE_COMMAND_8, 0xE3,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

    WRITE_C8_D16, 0xE4, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 16,
    0x0A, 0xE9, 0xD8, 0xA0,
    0x0C, 0xEB, 0xD8, 0xA0,
    0x0E, 0xED, 0xD8, 0xA0,
    0x10, 0xEF, 0xD8, 0xA0,

    WRITE_COMMAND_8, 0xE6,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

    WRITE_C8_D16, 0xE7, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE8,
    WRITE_BYTES, 16,
    0x09, 0xE8, 0xD8, 0xA0,
    0x0B, 0xEA, 0xD8, 0xA0,
    0x0D, 0xEC, 0xD8, 0xA0,
    0x0F, 0xEE, 0xD8, 0xA0,

    WRITE_COMMAND_8, 0xEB,
    WRITE_BYTES, 7,
    0x02, 0x00, 0xE4, 0xE4,
    0x88, 0x00, 0x40,

    WRITE_C8_D16, 0xEC, 0x3C, 0x00,

    WRITE_COMMAND_8, 0xED,
    WRITE_BYTES, 16,
    0xAB, 0x89, 0x76, 0x54,
    0x02, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x20,
    0x45, 0x67, 0x98, 0xBA,

    //-----------VAP & VAN---------------
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x13,

    WRITE_C8_D8, 0xE5, 0xE4,

    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,

    WRITE_C8_D8, 0x3A, 0x60, // 0x70 RGB888, 0x60 RGB666, 0x50 RGB565

    //WRITE_COMMAND_8, 0x21,0X00,
    //END_WRITE,
     
    DELAY, 10,
    WRITE_COMMAND_8, 0x11, // Sleep Out
    END_WRITE,

    DELAY, 120,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29, // Display On

    END_WRITE};

Arduino_ST7701_RGBPanel *gfx = new Arduino_ST7701_RGBPanel(
  bus, GFX_NOT_DEFINED, 0, true /* IPS */, 480, 480,
  herstellerInit, sizeof(herstellerInit), true /* BGR */,
  10, 8, 50, 10, 8, 20);

// ---------- gemeinsamer Zustand ----------
static Einst ein;
static Lage lage;                       // vom Netz-Task gefüllt, unter Sperre
static Verlauf verl;                    // vom Netz-Task geschrieben, unter Sperre
static Verlauf verlUI;                  // Kopie fürs Zeichnen
static Oberflaeche ui;
static SemaphoreHandle_t sperre;

enum { A_SCAN = 1, A_VERBINDEN = 2, A_SUCHE = 4, A_ZEIT = 8, A_UPD_PRUEFEN = 16, A_UPD_LADEN = 32 };
static volatile uint32_t anfragen = 0;
static portMUX_TYPE anfrageMux = portMUX_INITIALIZER_UNLOCKED;
static void anfrage(uint32_t bit) { portENTER_CRITICAL(&anfrageMux); anfragen |= bit; portEXIT_CRITICAL(&anfrageMux); }
static uint32_t holeAnfragen() {
  portENTER_CRITICAL(&anfrageMux); uint32_t a = anfragen; anfragen = 0; portEXIT_CRITICAL(&anfrageMux);
  return a;
}
static volatile uint32_t letzteOk = 0;  // millis() des letzten guten Berichts, 0 = nie
static volatile bool datenNeu = false, listeNeu = false;
static volatile int verbindErgebnis = 0;    // 0 nichts, 2 ok, 3 Fehler
static volatile bool sucheFertig = false;

static uint16_t *fb = nullptr, *basisBuf = nullptr, *arbeitBuf = nullptr, *teilBuf = nullptr;
// Zwei Leinwände: "basis" ist das, was gerade zu sehen ist (Grund für die
// Striche), in "arbeit" wird die nächste Seite gezeichnet. So laufen die
// Striche weiter, während neu gezeichnet wird - vorher stockten sie bei jedem
// Bericht (alle 3 s) für die Dauer des Neuzeichnens.
static Leinwand basis, arbeit;
static SemaphoreHandle_t bildSperre;
static bool ersterAufbau = true;      // beim ersten Bild alles kopieren   // fb und basis: Striche-Task gegen Seitenwechsel
static const int TEIL_PX = 8192;

static void sperren() { xSemaphoreTake(sperre, portMAX_DELAY); }
static void freigeben() { xSemaphoreGive(sperre); }

// =====================================================================
// Einstellungen
// =====================================================================
static void einstLaden() {
  Preferences prefs;   // je Aufruf eigenes Objekt - beide Kerne sichern
  prefs.begin("pvm", true);
  prefs.getString("ssid", ein.ssid, sizeof ein.ssid);
  prefs.getString("pass", ein.pass, sizeof ein.pass);
  prefs.getString("ip", ein.ip, sizeof ein.ip);
  prefs.getString("host", ein.host, sizeof ein.host);
  prefs.getString("prod", ein.produkt, sizeof ein.produkt);
  prefs.getString("plz", ein.plz, sizeof ein.plz);
  if (prefs.getBytesLength("peaks") == sizeof ein.peaks) prefs.getBytes("peaks", ein.peaks, sizeof ein.peaks);
  ein.cap = prefs.getInt("cap", 0);
  ein.nachtHell = prefs.getInt("hell", 20);
  ein.zone = prefs.getInt("zone", 0);
  ein.sprache = prefs.getInt("spr", 0);
  prefs.end();
  if (ein.zone < 0 || ein.zone >= ZONEN_ANZ) ein.zone = 0;
  sprache = ein.sprache ? 1 : 0;
}
static void einstSichern() {
  Preferences prefs;   // je Aufruf eigenes Objekt - beide Kerne sichern
  prefs.begin("pvm", false);
  prefs.putString("ssid", ein.ssid); prefs.putString("pass", ein.pass);
  prefs.putString("ip", ein.ip); prefs.putString("host", ein.host); prefs.putString("prod", ein.produkt); prefs.putString("plz", ein.plz);
  prefs.putBytes("peaks", ein.peaks, sizeof ein.peaks);
  prefs.putInt("cap", ein.cap); prefs.putInt("hell", ein.nachtHell);
  prefs.putInt("zone", ein.zone); prefs.putInt("spr", ein.sprache);
  prefs.end();
}
// Nur WLAN-Zugang zurückholen, z. B. nach einem misslungenen Wechsel
static void wlanAusSpeicher(char *ssid, char *pass) {
  Preferences prefs;   // je Aufruf eigenes Objekt - beide Kerne sichern
  prefs.begin("pvm", true);
  prefs.getString("ssid", ssid, 33); prefs.getString("pass", pass, 65);
  prefs.end();
}

// =====================================================================
// Verlauf: 5-Minuten-Mittel und Tagesenergie, alle 15 min gesichert
// =====================================================================
static int slotAkt = -1; static float sumS = 0, sumH = 0, sumL = 0, sumN2 = 0; static int sumN = 0;
// Auf- und Untergang lernt der Monitor aus dem eigenen Verlauf: erste und
// letzte Minute mit Solarleistung des Vortags. Er kennt keinen Standort, und
// so braucht es keine weitere Einstellung. Am ersten Tag ist die Sonne nicht zu sehen.
static int sonneAuf = -1, sonneUnter = -1;
static void sonneLernen(const Verlauf &v) {
  int a = -1, u = -1;
  for (int i = 0; i < SLOTS; i++) if (v.solar[i] > 5) { if (a < 0) a = i; u = i; }
  if (a < 0 || u - a < 24) return;                 // weniger als 2 h Sonne: nichts lernen
  sonneAuf = a * 5; sonneUnter = (u + 1) * 5;
  Preferences p; p.begin("sonne", false); p.putInt("auf", sonneAuf); p.putInt("unter", sonneUnter); p.end();
}
static uint32_t letzteBuchung = 0, letzteSicherung = 0;

// Verlauf einer Woche im Dateibereich (Partition "spiffs" ab 0x810000, LittleFS).
// Der liegt hinter dem Programm: weder das Update über WLAN noch das Flashen
// über USB aus der App schreibt dorthin - die Statistik bleibt also erhalten.
// Je Tag eine Datei /vJJJJMMTT.bin (2,3 KB), die sieben neuesten bleiben.
// Bis Firmware 20 lag nur ein Tag im NVS; der wird beim ersten Start übernommen.
static bool fsOk = false;
static const int TAGE = 7;
static void tagPfad(char *b, int n, uint32_t tag) { snprintf(b, n, "/v%08lu.bin", (unsigned long)tag); }
// Gespeicherte Tage, neuester zuerst
static int tageListe(uint32_t *t, int max) {
  int n = 0;
  if (!fsOk) return 0;
  File d = LittleFS.open("/");
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    const char *nm = f.name(); if (*nm == '/') nm++;
    if (nm[0] == 'v' && f.size() == sizeof(Verlauf)) {
      uint32_t tg = strtoul(nm + 1, nullptr, 10);
      if (tg > 20000000 && n < 32) t[n++] = tg;
    }
    f.close();
  }
  d.close();
  for (int i = 1; i < n; i++) for (int j = i; j > 0 && t[j] > t[j - 1]; j--) { uint32_t x = t[j]; t[j] = t[j - 1]; t[j - 1] = x; }
  return n < max ? n : max;
}
static bool tagLesen(uint32_t tag, Verlauf &v) {
  char p[24]; tagPfad(p, sizeof p, tag);
  File f = LittleFS.open(p, "r");
  if (!f) return false;
  bool ok = f.size() == sizeof(Verlauf) && f.read((uint8_t *)&v, sizeof(Verlauf)) == sizeof(Verlauf);
  f.close();
  return ok && v.tag == tag;
}
// Aufräumen bei jedem Start: Im Dateibereich bleibt nur, was die Firmware
// kennt - die sieben neuesten Tagesdateien in der aktuellen Größe. Alles
// andere (Reste abgebrochener Schreibvorgänge, Dateien eines älteren
// Formats, Versuche) wird gelöscht. So sammelt sich über die Jahre nichts an.
static void verlaufSichern();
static void aufraeumen() {
  if (!fsOk) return;
  uint32_t t[32]; int n = tageListe(t, 32);     // nur gültige, neueste zuerst
  char weg[16][32]; int w = 0;
  File d = LittleFS.open("/");
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    const char *nm = f.name(); if (*nm == '/') nm++;
    bool behalten = false;
    if (nm[0] == 'v' && f.size() == sizeof(Verlauf) && !f.isDirectory()) {
      uint32_t tg = strtoul(nm + 1, nullptr, 10);
      for (int i = 0; i < n && i < TAGE; i++) if (t[i] == tg) behalten = true;
    }
    if (!behalten && w < 16) snprintf(weg[w++], 32, "/%s", nm);
    f.close();
  }
  d.close();
  for (int i = 0; i < w; i++) { Serial.printf("Aufräumen: %s\n", weg[i]); LittleFS.remove(weg[i]) || LittleFS.rmdir(weg[i]); }
  Serial.printf("Dateibereich: %u von %u KB belegt\n", (unsigned)(LittleFS.usedBytes() / 1024), (unsigned)(LittleFS.totalBytes() / 1024));
}
static void verlaufLaden() {
  verl.leeren(0);
  uint32_t t[32];
  bool ausDatei = fsOk && tageListe(t, 32) > 0 && tagLesen(t[0], verl);
  Preferences prefs;   // Übernahme aus dem NVS (bis Firmware 20), danach dort löschen
  prefs.begin("verlauf", false);
  if (!ausDatei && prefs.getBytesLength("tag") == sizeof(Verlauf)) prefs.getBytes("tag", &verl, sizeof(Verlauf));
  if (fsOk) prefs.clear();
  prefs.end();
  if (fsOk && !ausDatei && verl.tag) verlaufSichern();
}
static void verlaufSichern() {
  if (!verl.tag) return;
  if (!fsOk) {                                   // Notlösung ohne Dateibereich
    Preferences prefs; prefs.begin("verlauf", false);
    prefs.putBytes("tag", &verl, sizeof(Verlauf)); prefs.end();
    return;
  }
  // erst in eine Hilfsdatei, dann umbenennen - ein Stromausfall mitten im
  // Schreiben lässt so den alten Stand stehen
  char p[24]; tagPfad(p, sizeof p, verl.tag);
  File f = LittleFS.open("/neu.tmp", "w");
  if (!f) return;
  bool ok = f.write((const uint8_t *)&verl, sizeof(Verlauf)) == sizeof(Verlauf);
  f.close();
  if (!ok) return;
  // LittleFS ersetzt beim Umbenennen eine vorhandene Datei in einem Schritt -
  // nicht vorher löschen, sonst gäbe es einen Moment ganz ohne den Tag
  LittleFS.rename("/neu.tmp", p);
  // Ältere als die sieben neuesten löschen
  uint32_t t[32]; int n = tageListe(t, 32);
  for (int i = TAGE; i < n; i++) { tagPfad(p, sizeof p, t[i]); LittleFS.remove(p); }
}
static Verlauf verlAlt;                  // ein Vortag für die Anzeige
static uint32_t verlAltTag = 0;
static void verlaufBuchen(const Werte &w) {
  time_t jetzt = time(nullptr); struct tm t; localtime_r(&jetzt, &t);
  if (t.tm_year < 124) return;                       // Uhr noch nicht gestellt
  uint32_t tag = (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
  uint32_t ms = millis();
  sperren();
  if (verl.tag != tag) {
    if (verl.tag) { verlaufSichern(); sonneLernen(verl); }   // gestern festhalten und daraus lernen
    verl.leeren(tag); slotAkt = -1;
  }
  int slot = (t.tm_hour * 60 + t.tm_min) / 5;
  if (slot != slotAkt) { slotAkt = slot; sumS = sumH = sumL = sumN2 = 0; sumN = 0; }
  float solar = w.n(F_SOLAR), haus = w.n(F_HOME), laden = w.n(F_LADEN), netz = w.n(F_GRID);
  sumS += solar; sumH += haus; sumL += laden; sumN2 += netz; sumN++;
  verl.solar[slot] = (int16_t)lroundf(sumS / sumN);
  verl.haus[slot] = (int16_t)lroundf(sumH / sumN);
  verl.laden[slot] = (int16_t)lroundf(sumL / sumN);
  verl.netz[slot] = (int16_t)lroundf(sumN2 / sumN);
  if (letzteBuchung) {
    uint32_t d = ms - letzteBuchung; if (d > 30000) d = 30000;   // Lücken nicht hochrechnen
    float h = d / 3600000.0f;
    verl.whSolar += solar * h; verl.whHaus += haus * h; verl.whNetz += netz * h;
  }
  letzteBuchung = ms;
  bool sichern = ms - letzteSicherung > 15UL * 60 * 1000;
  freigeben();
  if (sichern) { letzteSicherung = ms; sperren(); verlaufSichern(); freigeben(); }
}

// =====================================================================
// Netz-Task (Kern 0)
// =====================================================================
static bool zeitGestellt() { time_t j = time(nullptr); struct tm t; localtime_r(&j, &t); return t.tm_year >= 124; }

static void scanNetze() {
  int n = WiFi.scanNetworks(false, false);
  Netz tmp[24]; int anz = 0;
  for (int i = 0; i < n && anz < 24; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    int dup = -1;
    for (int k = 0; k < anz; k++) if (!strcmp(tmp[k].ssid, s.c_str())) dup = k;
    if (dup >= 0) { if (WiFi.RSSI(i) > tmp[dup].rssi) tmp[dup].rssi = WiFi.RSSI(i); continue; }
    snprintf(tmp[anz].ssid, 33, "%s", s.c_str());
    tmp[anz].rssi = WiFi.RSSI(i);
    tmp[anz].offen = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    anz++;
  }
  WiFi.scanDelete();
  for (int i = 0; i < anz; i++) for (int k = i + 1; k < anz; k++)
    if (tmp[k].rssi > tmp[i].rssi) { Netz x = tmp[i]; tmp[i] = tmp[k]; tmp[k] = x; }
  sperren();
  memcpy(lage.netze, tmp, sizeof(Netz) * anz); lage.netzAnz = anz; lage.scanLaeuft = false;
  freigeben();
  listeNeu = true;
  Serial.printf("WLAN: %d Netze gefunden\n", anz);
}

static bool verbinden(const char *ssid, const char *pass, uint32_t warteMs) {
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(ssid, pass);
  uint32_t t0 = millis();
  while (millis() - t0 < warteMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(200);
  }
  return false;
}

static bool mdnsLaeuft = false;
static void mdnsStart() {
  if (!mdnsLaeuft && MDNS.begin("pv-monitor")) mdnsLaeuft = true;
}

// Ein Bericht vom Speicher. Gibt true zurück, wenn es einer war.
static bool holeBericht(const char *ip, Werte &w, int timeoutMs) {
  HTTPClient http;
  char url[64]; snprintf(url, sizeof url, "http://%s%s", ip, ZENDURE_PFAD);
  http.setConnectTimeout(timeoutMs); http.setTimeout(timeoutMs);
  if (!http.begin(url)) return false;
  int code = http.GET();
  bool ok = false;
  if (code == 200) { String b = http.getString(); ok = leseBericht(b.c_str(), w); }
  http.end();
  return ok;
}

// Alle Adressen des eigenen /24-Netzes auf Port 80 abklopfen, je 8 zugleich.
// Gibt die letzte Stelle jeder Adresse zurück, die eine Verbindung annimmt.
static int klopfeNetz(uint8_t *offen, int max) {
  IPAddress ich = WiFi.localIP();
  const int ZUGLEICH = 8;                 // lwIP hat nur wenige Sockets frei
  int anz = 0;
  for (int start = 1; start < 255; start += ZUGLEICH) {
    int fd[ZUGLEICH], nr[ZUGLEICH], n = 0;
    for (int i = start; i < start + ZUGLEICH && i < 255; i++) {
      if (i == ich[3]) continue;
      int so = socket(AF_INET, SOCK_STREAM, 0);
      if (so < 0) continue;
      fcntl(so, F_SETFL, O_NONBLOCK);
      struct sockaddr_in a; memset(&a, 0, sizeof a);
      a.sin_family = AF_INET; a.sin_port = htons(80);
      a.sin_addr.s_addr = (uint32_t)IPAddress(ich[0], ich[1], ich[2], i);
      connect(so, (struct sockaddr *)&a, sizeof a);
      fd[n] = so; nr[n] = i; n++;
    }
    bool fertig[ZUGLEICH] = {};
    uint32_t t0 = millis();
    while (millis() - t0 < 500) {
      fd_set w; FD_ZERO(&w); int hoch = -1;
      for (int k = 0; k < n; k++) if (!fertig[k]) { FD_SET(fd[k], &w); if (fd[k] > hoch) hoch = fd[k]; }
      if (hoch < 0) break;
      struct timeval tv = {0, 50000};
      if (select(hoch + 1, nullptr, &w, nullptr, &tv) <= 0) continue;
      for (int k = 0; k < n; k++) if (!fertig[k] && FD_ISSET(fd[k], &w)) {
        int fehler = 0; socklen_t l = sizeof fehler;
        getsockopt(fd[k], SOL_SOCKET, SO_ERROR, &fehler, &l);
        fertig[k] = true;
        if (!fehler && anz < max) offen[anz++] = (uint8_t)nr[k];
      }
    }
    for (int k = 0; k < n; k++) close(fd[k]);
  }
  return anz;
}

// Wie die App: erst mDNS, dann immer das ganze Netz absuchen. Manche Geräte
// melden sich per mDNS nicht oder nicht rechtzeitig - beim ersten echten Test
// am 24.11. fand mDNS allein den Speicher nicht.
static void sucheSpeicher() {
  mdnsStart();
  Fund tmp[6]; int anz = 0, antworten = 0;
  static Werte probe;
  // Bis zu dreimal fragen, mit Geduld: Kleine Webserver wie der im SolarFlow
  // brauchen nach einer kurz geöffneten und gleich wieder geschlossenen
  // Verbindung (dem Anklopfen) einen Moment, bevor sie wieder antworten.
  auto aufnehmen = [&](const char *host, const char *ip) {
    for (int k = 0; k < anz; k++) if (!strcmp(tmp[k].ip, ip)) return;
    if (anz >= 6) return;
    bool ok = false;
    for (int v = 0; v < 3 && !ok; v++) { if (v) delay(600); ok = holeBericht(ip, probe, 3500); }
    if (!ok) return;
    antworten++;
    Fund &f = tmp[anz++];
    snprintf(f.host, sizeof f.host, "%s", host);
    snprintf(f.ip, sizeof f.ip, "%s", ip);
    snprintf(f.produkt, sizeof f.produkt, "%s", probe.produkt);
  };
  // Der Speicher, der gerade Daten liefert, gehört immer dazu
  char akt[16]; sperren(); snprintf(akt, sizeof akt, "%s", ein.ip); freigeben();
  if (akt[0]) aufnehmen("", akt);
  int n = mdnsLaeuft ? MDNS.queryService("zendure", "tcp") : 0;
  for (int i = 0; i < n; i++) aufnehmen(MDNS.hostname(i).c_str(), MDNS.IP(i).toString().c_str());
  Serial.printf("Suche: mDNS %d Treffer\n", n);
  uint8_t offen[40];
  int k = klopfeNetz(offen, 40);
  Serial.printf("Suche: %d Geraete mit Port 80\n", k);
  delay(1500);                                   // den Geräten Zeit geben, sich zu fangen
  IPAddress ich = WiFi.localIP();
  for (int i = 0; i < k; i++) {
    char ip[16]; snprintf(ip, sizeof ip, "%d.%d.%d.%d", ich[0], ich[1], ich[2], offen[i]);
    aufnehmen("", ip);
  }
  sperren();
  memcpy(lage.funde, tmp, sizeof(Fund) * anz); lage.fundAnz = anz; lage.sucheLaeuft = false;
  lage.sucheMdns = n; lage.suchePort = k; lage.sucheAntwort = antworten;
  freigeben();
  sucheFertig = true; listeNeu = true;
  Serial.printf("Suche: %d Speicher gefunden\n", anz);
}

// =====================================================================
// Update über WLAN aus den GitHub-Freigaben
// =====================================================================
// Der Bau legt neben dem USB-Abbild (alles ab 0x0) die reine Programmdatei
// PV-Monitor_Update_Build-N.bin an die Freigabe fw-N. Nur die wird hier
// geladen und in den freien App-Bereich geschrieben; Vorlader, Partitionen
// und NVS bleiben unberührt - WLAN, Speicher und Verlauf überleben.
// Verbindung nur mit geprüftem Zertifikat (zertifikate.h), Quelle fest
// eingetragen.
static const char *UPD_LISTE = "https://api.github.com/repos/student100tech/balkonstrom/releases?per_page=20";
static const char *UPD_QUELLE = "https://github.com/student100tech/balkonstrom/releases/download/fw-%d/PV-Monitor_Update_Build-%d.bin";

static void updStatus(int st, const char *fehler = nullptr) {
  sperren();
  lage.updStatus = st;
  if (fehler) snprintf(lage.updFehler, sizeof lage.updFehler, "%s", fehler);
  freigeben();
  listeNeu = true;
}
static void updatePruefen() {
  updStatus(1);
  WiFiClientSecure cl; cl.setCACert(GITHUB_WURZELN);
  HTTPClient http; http.setConnectTimeout(8000); http.setTimeout(12000);
  int neuestes = 0; bool ok = false;
  if (http.begin(cl, UPD_LISTE)) {
    http.setUserAgent("PV-Monitor");
    http.addHeader("Accept", "application/vnd.github+json");
    int code = http.GET();
    if (code == 200) {
      String t = http.getString();
      // Statt das ganze JSON zu zerlegen: nach den Dateinamen suchen
      const char *m = "PV-Monitor_Update_Build-";
      int i = 0;
      while ((i = t.indexOf(m, i)) >= 0) {
        i += strlen(m);
        int n = atoi(t.c_str() + i);
        if (n > neuestes) neuestes = n;
      }
      ok = true;
    } else Serial.printf("Update: GitHub antwortet %d\n", code);
    http.end();
  }
  sperren(); lage.updGeprueft = ok; if (ok) { lage.updBuild = neuestes; lage.updZeit = time(nullptr); } freigeben();
  updStatus(ok ? 0 : 4, ok ? nullptr : "GitHub nicht erreichbar");
  if (ok) Serial.printf("Update: neuestes Build %d, installiert %d\n", neuestes, FW_BUILD);
}
static void updateLaden() {
  int n; sperren(); n = lage.updBuild; lage.updProzent = 0; freigeben();
  if (n <= FW_BUILD) return;
  updStatus(2);
  char url[200]; snprintf(url, sizeof url, UPD_QUELLE, n, n);
  WiFiClientSecure cl; cl.setCACert(GITHUB_WURZELN);
  HTTPClient http; http.setConnectTimeout(8000); http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);     // GitHub leitet auf seinen Dateispeicher um
  const char *fehler = nullptr;
  if (!http.begin(cl, url)) fehler = "Adresse ungueltig";
  else {
    http.setUserAgent("PV-Monitor");
    int code = http.GET(), len = http.getSize();
    if (code != 200 || len <= 0) { fehler = "Datei nicht gefunden"; Serial.printf("Update: HTTP %d\n", code); }
    else if (!Update.begin(len, U_FLASH)) fehler = "kein Platz fuer das Update";
    else {
      WiFiClient *st = http.getStreamPtr();
      static uint8_t puf[4096];
      int geschrieben = 0; uint32_t zuletzt = millis();
      while (geschrieben < len && millis() - zuletzt < 20000) {
        int v = st->available();
        if (v <= 0) { delay(5); continue; }
        int r = st->readBytes(puf, v < (int)sizeof puf ? v : sizeof puf);
        if (r <= 0) continue;
        if (Update.write(puf, r) != (size_t)r) { fehler = "Schreiben fehlgeschlagen"; break; }
        geschrieben += r; zuletzt = millis();
        int pz = (int)((int64_t)geschrieben * 100 / len);
        sperren(); if (pz != lage.updProzent) { lage.updProzent = pz; listeNeu = true; } freigeben();
      }
      if (!fehler && geschrieben < len) fehler = "Download abgebrochen";
      // end(true) prüft auch die Prüfsumme im Programmabbild selbst
      if (!fehler && !Update.end(true)) fehler = "Abbild ungueltig";
      if (fehler) Update.abort();
    }
    http.end();
  }
  if (fehler) { Serial.printf("Update: %s\n", fehler); updStatus(4, fehler); return; }
  Serial.printf("Update: Build %d geschrieben, Neustart\n", n);
  { Preferences p; p.begin("ota", false); p.putInt("neu", 1); p.putInt("versuch", 0); p.end(); }
  sperren(); verlaufSichern(); freigeben();
  updStatus(3);
  delay(1500);
  ESP.restart();
}
// Läuft eine frisch geladene Fassung nicht stabil an, zurück zur alten.
// Der Vorlader des Arduino-Kerns kann das nicht selbst, also zählt die
// Firmware ihre eigenen Startversuche: Drei Abstürze vor der Bestätigung nach
// 60 s, und der vorige App-Bereich wird wieder Startbereich.
static void startPruefen() {
  Preferences p; p.begin("ota", false);
  if (p.getInt("neu", 0)) {
    int v = p.getInt("versuch", 0) + 1; p.putInt("versuch", v);
    if (v > 3) {
      p.putInt("neu", 0); p.putInt("versuch", 0); p.end();
      const esp_partition_t *alt = esp_ota_get_next_update_partition(nullptr);
      Serial.println("Update: neue Fassung startet nicht, zurueck zur alten");
      if (alt && esp_ota_set_boot_partition(alt) == ESP_OK) { delay(100); ESP.restart(); }
      return;
    }
  }
  p.end();
}
static void startBestaetigen() {
  Preferences p; p.begin("ota", false);
  if (p.getInt("neu", 0)) { p.putInt("neu", 0); p.putInt("versuch", 0); Serial.println("Update: neue Fassung laeuft stabil"); }
  p.end();
}

// Demomodus: dreimal auf Logo/PV-Monitor tippen. Ein 800 Pro 2 mit allem
// gleichzeitig - vier Eingänge, Off-Grid-Dose, Akku entlädt, Heizung an. Die
// Werte schwanken leicht, damit sich etwas bewegt. Nicht gespeichert, nach
// einem Neustart ist er aus; der Verlauf bleibt unberührt.
static volatile bool demoAn = false;
static bool demoBericht(Werte &w) {
  float t = millis() / 1000.0f;
  int p1 = 210 + (int)(18 * sinf(t / 7)), p2 = 180 + (int)(15 * sinf(t / 9 + 1));
  int p3 = 160 + (int)(12 * sinf(t / 8 + 2)), p4 = 140 + (int)(10 * sinf(t / 11 + 3));
  int solar = p1 + p2 + p3 + p4, haus = 800, dose = 120 + (int)(8 * sinf(t / 5));
  int ent = haus + dose - solar; if (ent < 0) ent = 0;
  static char j[1100];
  snprintf(j, sizeof j,
    "{\"sn\":\"DEMO\",\"product\":\"solarFlow800Pro2\",\"properties\":{"
    "\"solarInputPower\":%d,\"solarPower1\":%d,\"solarPower2\":%d,\"solarPower3\":%d,\"solarPower4\":%d,"
    "\"outputHomePower\":%d,\"packInputPower\":%d,\"outputPackPower\":0,\"gridInputPower\":0,\"electricLevel\":64,"
    "\"remainOutTime\":320,\"hyperTmp\":3121,\"BatVolt\":5120,\"acMode\":2,\"inputLimit\":0,\"outputLimit\":800,"
    "\"socSet\":900,\"minSoc\":150,\"inverseMaxPower\":800,\"chargeMaxLimit\":1200,\"rssi\":-58,\"packNum\":2,"
    "\"packState\":2,\"faultLevel\":1,\"is_error\":0,\"heatState\":1,\"derateFlag\":0,\"gridOffMode\":1,"
    "\"gridOffPower\":%d,\"Fanmode\":1,\"Fanspeed\":2},"
    "\"packData\":[{\"maxTemp\":2891,\"maxVol\":331,\"minVol\":329,\"totalVol\":4965,\"batcur\":65509,\"power\":%d,\"socLevel\":64},"
    "{\"maxTemp\":2884,\"maxVol\":331,\"minVol\":329,\"totalVol\":4962,\"batcur\":65512,\"power\":%d,\"socLevel\":64}]}",
    solar, p1, p2, p3, p4, haus, ent, dose, ent / 2, ent - ent / 2);
  return leseBericht(j, w);
}
static bool ip0() { bool b; sperren(); b = ein.ip[0] != 0; freigeben(); return b; }
static void netzTask(void *) {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.setHostname("PV-Monitor");
  if (ein.ssid[0]) { Serial.println("WLAN: verbinde"); WiFi.begin(ein.ssid, ein.pass); }
  bool warVerbunden = false, zeitAngestossen = false, zeitGemeldet = false;
  uint32_t letzteAbfrage = 0; int fehler = 0; bool ersteAntwort = true;
  for (;;) {
    uint32_t a = holeAnfragen();
    if (a & A_SCAN) scanNetze();
    if (a & A_VERBINDEN) {
      char ssid[33], pass[65];
      sperren(); snprintf(ssid, sizeof ssid, "%s", ein.ssid); snprintf(pass, sizeof pass, "%s", ein.pass); freigeben();
      Serial.println("WLAN: verbinde mit neuem Netz");
      if (verbinden(ssid, pass, 20000)) {
        Serial.println("WLAN: verbunden");
        einstSichern(); verbindErgebnis = 2;
      } else {
        Serial.println("WLAN: Verbindung fehlgeschlagen");
        // Altes Netz wiederherstellen, damit der Monitor nicht ohne WLAN dasteht
        char s0[33], p0[65]; wlanAusSpeicher(s0, p0);
        sperren(); snprintf(ein.ssid, sizeof ein.ssid, "%s", s0); snprintf(ein.pass, sizeof ein.pass, "%s", p0); freigeben();
        if (s0[0]) WiFi.begin(s0, p0);
        verbindErgebnis = 3;
      }
    }
    bool verbunden = WiFi.status() == WL_CONNECTED;
    if (verbunden && !warVerbunden) {
      Serial.printf("WLAN: verbunden nach %lu ms\n", (unsigned long)millis());
      mdnsStart();
      a |= A_ZEIT;
    }
    warVerbunden = verbunden;
    if ((a & A_ZEIT) && verbunden) {
      configTzTime(ZONEN[ein.zone].tz, "pool.ntp.org", "time.cloudflare.com", "time.google.com");
      zeitAngestossen = true;
    }
    if (zeitAngestossen && !zeitGemeldet && zeitGestellt()) { zeitGemeldet = true; Serial.println("Zeit: gestellt"); }
    // Selbst nachsehen: gleich nach dem Start, dann stündlich; ist GitHub nicht
    // erreichbar, nach 2 Minuten wieder (bisher erst nach 6 Stunden).
    static uint32_t letztePruefung = 0; static bool erstePruefung = true, pruefOk = false;
    uint32_t abstand = pruefOk ? 3600UL * 1000 : 120UL * 1000;
    // Die erste Prüfung erst nach dem ersten Bericht (oder 60 s nach dem
    // Verbinden): Sie dauert bis zu 20 s und hielt sonst die erste Anzeige auf.
    static uint32_t verbundenSeit = 0;
    if (!verbunden) verbundenSeit = 0; else if (!verbundenSeit) verbundenSeit = millis();
    bool ersteFrei = zeitGestellt() && (!ersteAntwort || !ip0() || millis() - verbundenSeit > 60000);
    if (verbunden && (erstePruefung ? ersteFrei : millis() - letztePruefung > abstand)) a |= A_UPD_PRUEFEN;
    if (verbunden && (a & A_UPD_PRUEFEN)) {
      erstePruefung = false; letztePruefung = millis(); updatePruefen();
      sperren(); pruefOk = lage.updGeprueft; freigeben();
    }
    else if (a & A_UPD_PRUEFEN) updStatus(4, "kein WLAN");
    if (verbunden && (a & A_UPD_LADEN)) updateLaden();
    // Suche erst, wenn das WLAN steht - nach dem Einschalten dauert das ein paar Sekunden
    static uint32_t sucheSeit = 0;
    if (a & A_SUCHE) {
      if (verbunden) { sucheSeit = 0; sucheSpeicher(); }
      else {
        if (!sucheSeit) sucheSeit = millis();
        if (millis() - sucheSeit < 25000) anfrage(A_SUCHE);
        else { sucheSeit = 0; sperren(); lage.sucheLaeuft = false; lage.fundAnz = 0; freigeben(); sucheFertig = true; listeNeu = true; }
      }
    }

    // Abfrage alle 3 s
    char ip[16]; sperren(); snprintf(ip, sizeof ip, "%s", ein.ip); freigeben();
    if (demoAn) {
      if (millis() - letzteAbfrage >= ZENDURE_ABSTAND_S * 1000UL) {
        letzteAbfrage = millis();
        static Werte dw;
        if (demoBericht(dw)) { sperren(); lage.w = dw; freigeben(); letzteOk = millis(); datenNeu = true; }
      }
    } else if (verbunden && ip[0] && millis() - letzteAbfrage >= ZENDURE_ABSTAND_S * 1000UL) {
      letzteAbfrage = millis();
      static Werte w;
      if (holeBericht(ip, w, 2500)) {
        if (ersteAntwort || fehler >= 5) Serial.printf("Speicher: Antwort ok nach %lu ms\n", (unsigned long)millis());
        ersteAntwort = false; fehler = 0;
        sperren(); lage.w = w; freigeben();
        letzteOk = millis(); datenNeu = true;
        verlaufBuchen(w);
      } else if (++fehler == 5 || fehler % 20 == 0) {
        Serial.printf("Speicher: keine Antwort (%d)\n", fehler);
        // Neue Adresse vom Router? Über den mDNS-Namen nachschlagen.
        char host[48]; sperren(); snprintf(host, sizeof host, "%s", ein.host); freigeben();
        if (host[0] && mdnsLaeuft) {
          IPAddress neu = MDNS.queryHost(host, 2000);
          if (neu != IPAddress(0, 0, 0, 0) && strcmp(neu.toString().c_str(), ip)) {
            sperren(); snprintf(ein.ip, sizeof ein.ip, "%s", neu.toString().c_str()); freigeben();
            einstSichern(); Serial.println("Speicher: neue Adresse uebernommen");
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(40));
  }
}

// =====================================================================
// Touch (GT911)
// =====================================================================
static uint8_t touchAdr = 0;
static uint8_t sucheTouch() {
  for (uint8_t i = 0; i < 2; i++) {
    Wire.beginTransmission(GT911_ADDRS[i]);
    if (Wire.endTransmission() == 0) return GT911_ADDRS[i];
  }
  return 0;
}
// -1 nichts Neues, 0 losgelassen, 1 berührt (x/y im Bild)
static int liesTouch(int &x, int &y) {
  if (!touchAdr) return -1;
  Wire.beginTransmission(touchAdr);
  Wire.write(0x81); Wire.write(0x4E);
  if (Wire.endTransmission() != 0 || Wire.requestFrom((int)touchAdr, 1) != 1) return -1;
  uint8_t st = Wire.read();
  if (!(st & 0x80)) return -1;
  int erg = 0;
  if (st & 0x0F) {
    Wire.beginTransmission(touchAdr);
    Wire.write(0x81); Wire.write(0x50);
    if (Wire.endTransmission() == 0 && Wire.requestFrom((int)touchAdr, 4) == 4) {
      uint8_t xl = Wire.read(), xh = Wire.read(), yl = Wire.read(), yh = Wire.read();
      int px = xl | (xh << 8), py = yl | (yh << 8);
      if (px < 480 && py < 480) { x = 479 - py; y = px; erg = 1; }   // Panel -> Bild (Build 7 bestätigt)
    }
  }
  Wire.beginTransmission(touchAdr);
  Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
  Wire.endTransmission();
  return erg;
}

// =====================================================================
// Licht
// =====================================================================
static uint32_t letzteBeruehrung = 0;
static int lichtJetzt = -1;
static bool nachts() {
  time_t j = time(nullptr); struct tm t; localtime_r(&j, &t);
  if (t.tm_year < 124) return false;
  int m = t.tm_hour * 60 + t.tm_min;
  return m >= 22 * 60 + 30 || m < 6 * 60 + 30;
}
static void lichtStellen() {
  int p = (nachts() && millis() - letzteBeruehrung > 30000) ? ein.nachtHell : 100;
  if (!ui.hauptseite()) p = 100;
  if (p == lichtJetzt) return;
  lichtJetzt = p;
  ledcWrite(0, p * 255 / 100);
}

// =====================================================================
// Bild: Leinwand -> Bildspeicher des Panels
// =====================================================================
// Eine neue Seite in Häppchen von 16 Zeilen übertragen, mit kurzer Pause
// dazwischen: Ein Kopieren von 460 KB am Stück nimmt dem Panel den Zugriff auf
// den PSRAM, und das Bild zittert.
// Nur die Häppchen kopieren, die sich gegenüber dem bisher gezeigten Bild
// (alt) geändert haben. Bei einem Bericht sind das wenige Zahlen - vorher
// wurde jedes Mal der ganze Bildspeicher (460 KB) neu beschrieben. Das
// Schreiben ins PSRAM konkurriert mit dem Panel, das von dort liest; zu viel
// davon auf einmal und das Panel bekommt kurz keine Daten (schwarzes Blitzen).
static void allesZeigen(const uint16_t *alt) {
  const int Z = 8;
  for (int r = 0; r < 480; r += Z) {
    uint16_t *q = basis.buf + r * 480, *d = fb + r * 480;
    if (alt && memcmp(q, alt + r * 480, Z * 480 * 2) == 0) continue;
    memcpy(d, q, Z * 480 * 2);
    Cache_WriteBack_Addr((uint32_t)d, Z * 480 * 2);
    delayMicroseconds(400);
  }
}
static void teilZeigen(Leinwand &q, int x, int y, int w, int h) {
  for (int xx = x; xx < x + w; xx++) memcpy(fb + (479 - xx) * 480 + y, q.an(xx, y), h * 2);
  Cache_WriteBack_Addr((uint32_t)(fb + (479 - (x + w - 1)) * 480), w * 480 * 2);
}

// Striche: erst alle Drähte fertig in den Teilspeicher rechnen, dann in einem
// Zug kopieren - so fehlen sie auch beim Neuzeichnen der Seite nie sichtbar.
struct Teil { Leinwand L; int x, y, w, h; };
static Teil teile[8]; static int teilAnz = 0;
// Abzug der Drähte zum Zeitpunkt des Seitenwechsels - der Striche-Task liest
// nur diesen, nie ui direkt (ui gehört der Hauptschleife).
static Draht strDraht[8]; static volatile int strAnz = 0; static bool strTabs = false;
static void stricheRechnen(uint32_t ms) {
  teilAnz = 0; int belegt = 0;
  for (int i = 0; i < strAnz; i++) {
    Teil &t = teile[teilAnz];
    Oberflaeche::drahtRahmen(strDraht[i], t.x, t.y, t.w, t.h);
    if (belegt + t.w * t.h > TEIL_PX) break;
    t.L.init(teilBuf + belegt, t.x, t.y, t.w, t.h);
    belegt += t.w * t.h;
    for (int xx = t.x; xx < t.x + t.w; xx++) memcpy(t.L.an(xx, t.y), basis.an(xx, t.y), t.h * 2);
    if (strTabs) t.L.beschnitt(t.x, t.y, t.x + t.w, 400);   // nicht über die Reiterleiste
    Oberflaeche::striche(t.L, strDraht[i], ms);
    teilAnz++;
  }
}
static void stricheZeigen() { for (int i = 0; i < teilAnz; i++) teilZeigen(teile[i].L, teile[i].x, teile[i].y, teile[i].w, teile[i].h); }

static Lage zeichenLage;
static void lageHolen() {
  sperren();
  zeichenLage = lage;
  bool vorTag = ui.seite == S_VERLAUF && ui.verlTag > 0;
  if (ui.seite == S_VERLAUF && !vorTag) verlUI = verl;
  uint32_t heuteTag = verl.tag;
  freigeben();
  if (ui.seite == S_VERLAUF) {
    // Tage zählen: Dateien plus heute (heute liegt evtl. noch nicht als Datei vor)
    uint32_t t[32]; int n = tageListe(t, 32), k = 0; uint32_t liste[TAGE]; 
    if (heuteTag) liste[k++] = heuteTag;
    for (int i = 0; i < n && k < TAGE; i++) if (t[i] != heuteTag) liste[k++] = t[i];
    zeichenLage.verlTage = k ? k : 1;
    if (vorTag && ui.verlTag < k) {
      uint32_t w = liste[ui.verlTag];
      if (w != verlAltTag) { if (tagLesen(w, verlAlt)) verlAltTag = w; else { verlAlt.leeren(0); verlAltTag = 0; } }
      verlUI = verlAlt;
    } else if (vorTag) verlUI.leeren(0);
  }
  uint32_t ok = letzteOk;
  zeichenLage.alterS = ok ? (int)((millis() - ok) / 1000) : -1;
  time_t j = time(nullptr); struct tm t; localtime_r(&j, &t);
  zeichenLage.uhr = t.tm_year >= 124;
  zeichenLage.hh = t.tm_hour; zeichenLage.mm = t.tm_min; zeichenLage.ss = t.tm_sec;
  if (zeichenLage.alterS >= 0) {
    time_t z = j - zeichenLage.alterS; struct tm u; localtime_r(&z, &u);
    zeichenLage.okHH = u.tm_hour; zeichenLage.okMM = u.tm_min; zeichenLage.okSS = u.tm_sec;
  }
  zeichenLage.build = FW_BUILD;
  zeichenLage.demo = demoAn;
  zeichenLage.zaehler = demoAn ? 340 + 25 * sinf(millis() / 6000.0f) : NAN;   // Smart Meter am Monitor: noch nicht, nur Demo
  zeichenLage.sonneF = -1;
  float lat, lon;
  if (zeichenLage.uhr && plzOrt(ein.plz, lat, lon)) {
    // Mit Postleitzahl: Auf- und Untergang gerechnet, dieselbe Formel wie
    // sunTimes() in der App (NOAA, Refraktion 90,833 Grad), in UTC-Sekunden.
    int y = t.tm_year + 1900, mo = t.tm_mon + 1, d = t.tm_mday;
    int n = t.tm_yday + 1;
    float gw = 2 * (float)M_PI / 365 * (n - 1), rad = (float)M_PI / 180;
    float eqt = 229.18f * (0.000075f + 0.001868f * cosf(gw) - 0.032077f * sinf(gw) - 0.014615f * cosf(2 * gw) - 0.040849f * sinf(2 * gw));
    float decl = 0.006918f - 0.399912f * cosf(gw) + 0.070257f * sinf(gw) - 0.006758f * cosf(2 * gw) + 0.000907f * sinf(2 * gw) - 0.002697f * cosf(3 * gw) + 0.00148f * sinf(3 * gw);
    float ha = acosf(cosf(90.833f * rad) / (cosf(lat * rad) * cosf(decl)) - tanf(lat * rad) * tanf(decl)) / rad;
    float mittag = 720 - 4 * lon - eqt;
    // Tage seit 1970 für das heutige Ortsdatum (Howard Hinnant, days_from_civil)
    int yy = mo <= 2 ? y - 1 : y, era = yy / 400, yoe = yy - era * 400;
    int doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    double mitte = (double)(era * 146097 + doe - 719468) * 86400;
    double auf = mitte + (mittag - 4 * ha) * 60, unter = mitte + (mittag + 4 * ha) * 60;
    double f = ((double)j - auf) / (unter - auf);
    if (!isnan(f) && f >= 0 && f <= 1) zeichenLage.sonneF = (float)f;
  } else if (zeichenLage.uhr && sonneAuf >= 0 && sonneUnter > sonneAuf) {
    // Ohne Postleitzahl: aus dem Verlauf des Vortags gelernt
    float m = t.tm_hour * 60 + t.tm_min + t.tm_sec / 60.0f;
    float f = (m - sonneAuf) / (sonneUnter - sonneAuf);
    if (f >= 0 && f <= 1) zeichenLage.sonneF = f;
  }
  zeichenLage.wlanOk = WiFi.status() == WL_CONNECTED;
  zeichenLage.wlanRssi = zeichenLage.wlanOk ? WiFi.RSSI() : 0;
  zeichenLage.verlauf = &verlUI;
}
static void seiteZeichnen() {
  lageHolen();
  arbeit.volleFlaeche();
  ui.zeichne(arbeit, zeichenLage);            // dauert - die Striche laufen derweil weiter
  xSemaphoreTake(bildSperre, portMAX_DELAY);
  Leinwand z = basis; basis = arbeit; arbeit = z;   // arbeit = bisher gezeigtes Bild
  int n = ui.seite == S_FLUSS ? ui.drahtAnz : 0;
  for (int i = 0; i < n; i++) strDraht[i] = ui.draht[i];
  strAnz = n; strTabs = ui.tabs;
  stricheRechnen(millis());
  allesZeigen(ersterAufbau ? nullptr : arbeit.buf);
  ersterAufbau = false;
  stricheZeigen();
  xSemaphoreGive(bildSperre);
}

static void stricheTask(void *) {
  TickType_t takt = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&takt, pdMS_TO_TICKS(33));
    if (!strAnz) continue;
    xSemaphoreTake(bildSperre, portMAX_DELAY);
    stricheRechnen(millis());
    stricheZeigen();
    xSemaphoreGive(bildSperre);
  }
}

// =====================================================================
// setup / loop
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nPV-Monitor Firmware Build %d\n", FW_BUILD);
  startPruefen();
  sperre = xSemaphoreCreateMutex();

  ledcSetup(0, 5000, 8); ledcAttachPin(PIN_LICHT, 0); ledcWrite(0, 0);
  // 12 statt 16 MHz Pixeltakt (rund 42 statt 56 Bilder je Sekunde), wie in der
  // ESPHome-Beschreibung dieses Boards. Das Panel liest sein Bild ununterbrochen
  // aus dem PSRAM; schreibt die Firmware gleichzeitig eine ganze Seite dorthin,
  // kommt es nicht mehr hinterher und das Bild zittert - genau das zeigte Build 12
  // bei jedem Antippen. Weniger Lesebedarf lässt mehr Luft.
  gfx->begin(12000000);
  // Farbumkehr AN (0x21). Mit der Startfolge des Herstellers stimmen die
  // Farbbits, das Bild war in Build 12/13 aber genau umgekehrt: weißer Grund,
  // blaues Solar, roter Akku - jede Farbe ihr Gegenstück. Dieses Panel braucht
  // die Umkehr, das Herstellerdemo offenbar mit anderer Einstellung nicht.
  gfx->invertDisplay(true);
  fb = gfx->getFramebuffer();
  basisBuf = (uint16_t *)ps_malloc(480 * 480 * 2);
  arbeitBuf = (uint16_t *)ps_malloc(480 * 480 * 2);
  teilBuf = (uint16_t *)heap_caps_malloc(TEIL_PX * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!basisBuf || !arbeitBuf || !teilBuf || !fb) { Serial.println("Speicher fuer Bild fehlt"); }
  basis.init(basisBuf, 0, 0, 480, 480);
  arbeit.init(arbeitBuf, 0, 0, 480, 480);
  bildSperre = xSemaphoreCreateMutex();

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  touchAdr = sucheTouch();
  Serial.println(touchAdr ? "Touch: gefunden" : "Touch: nicht gefunden");

  einstLaden();
  fsOk = LittleFS.begin(true, "/lfs", 10, "spiffs");   // beim allerersten Start wird formatiert
  Serial.printf("Dateibereich: %s\n", fsOk ? "ok" : "FEHLER");
  aufraeumen();
  verlaufLaden();
  { Preferences p; p.begin("sonne", true); sonneAuf = p.getInt("auf", -1); sonneUnter = p.getInt("unter", -1); p.end(); }
  ui.e = &ein;
  if (!ein.ssid[0]) {
    ui.einrichtung = true; ui.gehe(S_NETZE);
    lage.scanLaeuft = true; anfrage(A_SCAN);
  } else if (!ein.ip[0]) {
    ui.einrichtung = true; ui.gehe(S_SPEICHER);
    lage.sucheLaeuft = true; anfrage(A_SUCHE);
  } else ui.gehe(S_FLUSS);

  seiteZeichnen();
  ledcWrite(0, 255); lichtJetzt = 100;
  xTaskCreatePinnedToCore(netzTask, "netz", 12288, nullptr, 1, nullptr, 0);
  // Striche: eigener Task auf Kern 1, Vorrang vor der Hauptschleife (Priorität 1),
  // damit er sie auch mitten im Zeichnen einer Seite unterbricht.
  xTaskCreatePinnedToCore(stricheTask, "striche", 6144, nullptr, 2, nullptr, 1);
}

static bool unten = false, schlucken = false, gedrueckt = false;
static uint32_t tabsBis = 0;      // bis wann die Reiterleiste stehen bleibt
static int tx0, ty0, txl, tyl; static uint32_t t0 = 0, tZuletzt = 0;
static uint32_t letzteAnim = 0, letzteSekunde = 0, letzterTouch = 0, letztesLaufen = 0;
static int letzteSek = -1;

void loop() {
  uint32_t jetzt = millis();

  // ---- Touch ----
  // Auf Fluss, Zustand und Verlauf blendet die erste Berührung nur die
  // Reiterleiste ein; sie verschwindet 3 s nach der letzten Berührung wieder.
  // Gewischt wird nicht mehr (Entscheidung 25.11.).
  if (jetzt - letzterTouch >= 15) {
    letzterTouch = jetzt;
    int x, y, st = liesTouch(x, y);
    if (st == 1) {
      tZuletzt = jetzt;
      if (!unten) {
        unten = true; tx0 = txl = x; ty0 = tyl = y; t0 = jetzt;
        schlucken = lichtJetzt < 100;          // nachts weckt die erste Berührung nur das Licht
        letzteBeruehrung = jetzt; lichtStellen();
        // Rückmeldung: Knopf unter dem Finger sofort aufhellen
        // Auf Hauptseiten ohne Reiter wirkt ein Tipp auf einen Knopf sofort
        // (Regler, Felder, Logo); nur ein Tipp ins Leere blendet die Reiter ein.
        // Vorher schluckte der erste Tipp alles - und danach lag die Reiterleiste
        // genau über dem Helligkeitsregler, der so nie erreichbar war.
        if (!schlucken) {
          int i = ui.treffer(x, y);
          if (i >= 0) {
            const Knopf &k = ui.kn[i];
            int kx = max(0, (int)k.x), ky = max(0, (int)k.y);
            int kw = min(480, k.x + k.w) - kx, kh = min(480, k.y + k.h) - ky;
            if (kw > 0 && kh > 0) {
              xSemaphoreTake(bildSperre, portMAX_DELAY);
              basis.volleFlaeche();
              basis.hauch(kx, ky, kw, kh, 11, C_TEXT, 70);
              teilZeigen(basis, kx, ky, kw, kh);
              xSemaphoreGive(bildSperre);
              gedrueckt = true;
            }
          }
        }
      } else { txl = x; tyl = y; }
      if (ui.tabs) tabsBis = jetzt + 3000;
    }
    if (unten && (st == 0 || jetzt - tZuletzt > 150)) {
      unten = false;
      int dx = txl - tx0, dy = tyl - ty0;
      if (!schlucken && abs(dx) < 30 && abs(dy) < 30) {
        bool direkt = !(ui.hauptseite() && !ui.tabs) || ui.treffer(tx0, ty0) >= 0;
        if (!direkt) { ui.tabs = true; ui.neu = true; tabsBis = jetzt + 3000; }
        else {
          // Aufhellung mindestens 120 ms stehen lassen, sonst sieht man sie nicht
          if (gedrueckt && millis() - t0 < 120) delay(120 - (millis() - t0));
          bool warenDa = ui.tabs;
          ui.tippe(tx0, ty0, zeichenLage);
          if (warenDa && ui.hauptseite()) tabsBis = jetzt + 3000;
        }
      }
      if (gedrueckt) { gedrueckt = false; ui.neu = true; }   // Aufhellung wieder weg
    }
  }
  if (ui.tabs && (!ui.hauptseite() || (int32_t)(jetzt - tabsBis) > 0)) { ui.tabs = false; ui.neu = true; }

  // ---- Wünsche der Oberfläche ----
  uint32_t w = ui.wuensche; ui.wuensche = 0;
  if (w) {
    sprache = ein.sprache;
    if (w & W_SICHERN) einstSichern();
    if (w & W_SCAN) { sperren(); lage.scanLaeuft = true; freigeben(); anfrage(A_SCAN); }
    if (w & W_VERBINDEN) anfrage(A_VERBINDEN);
    if (w & W_SUCHE) { sperren(); lage.sucheLaeuft = true; lage.fundAnz = 0; freigeben(); anfrage(A_SUCHE); }
    if (w & W_GERAET) { sperren(); lage.w.leeren(); freigeben(); letzteOk = 0; }
    if (w & W_ZONE) anfrage(A_ZEIT);
    if (w & W_HELL) lichtJetzt = -1;
    if (w & W_UPD_PRUEFEN) anfrage(A_UPD_PRUEFEN);
    if (w & W_UPD_LADEN) anfrage(A_UPD_LADEN);
    if (w & W_LOGO) {
      // dreimal innerhalb von 2 s: Demomodus umschalten
      static uint32_t erstes = 0; static int zahl = 0;
      if (millis() - erstes > 2000) { erstes = millis(); zahl = 0; }
      if (++zahl >= 3) {
        zahl = 0; erstes = 0;
        demoAn = !demoAn;
        Serial.printf("Demo: %s\n", demoAn ? "an" : "aus");
        sperren(); lage.w.leeren(); freigeben(); letzteOk = 0;
        ui.gehe(S_FLUSS);
      }
    }
  }
  // ---- Ergebnisse des Netz-Tasks ----
  if (verbindErgebnis) {
    int e = verbindErgebnis; verbindErgebnis = 0;
    if (e == 2) {
      if (ui.einrichtung || !ein.ip[0]) {
        ui.einrichtung = true;
        sperren(); lage.sucheLaeuft = true; lage.fundAnz = 0; freigeben();
        anfrage(A_SUCHE); ui.gehe(S_SPEICHER);
      } else ui.gehe(S_FLUSS);
    } else ui.zeigeMeldung(T(T_VERB_FEHLER), true);
  }
  if (sucheFertig) {
    sucheFertig = false;
    sperren(); int n = lage.fundAnz; Fund f0 = lage.funde[0]; freigeben();
    // Beim ersten Einrichten und genau einem Speicher: nichts zu entscheiden
    if (ui.einrichtung && n == 1 && ui.seite == S_SPEICHER) {
      snprintf(ein.ip, sizeof ein.ip, "%s", f0.ip); snprintf(ein.host, sizeof ein.host, "%s", f0.host);
      snprintf(ein.produkt, sizeof ein.produkt, "%s", f0.produkt);
      einstSichern(); ui.einrichtung = false; ui.gehe(S_FLUSS);
    }
  }
  if (listeNeu) { listeNeu = false; if (ui.seite == S_NETZE || ui.seite == S_SPEICHER || ui.seite == S_UPDATE || ui.seite == S_EINST) ui.neu = true; }
  if (datenNeu) { datenNeu = false; if (ui.hauptseite() || ui.seite == S_EINST) ui.neu = true; }

  // ---- Zeichnen ----
  time_t tj = time(nullptr); struct tm tt; localtime_r(&tj, &tt);
  if (ui.neu) {
    seiteZeichnen();
    letzteSek = tt.tm_sec;
  } else if (tt.tm_sec != letzteSek) {
    letzteSek = tt.tm_sec;
    // "Stand" ändert sich nur mit neuen Daten (dann wird ohnehin gezeichnet).
    // Jede Sekunde wird nur geprüft, ob die Anzeige auf "Offline" springen muss;
    // der Verlauf bekommt einmal je Minute neue Punkte.
    static bool warOffline = false;
    uint32_t ok = letzteOk;
    bool offline = ok && millis() - ok > 10000;
    if (offline != warOffline || (ui.hauptseite() && tt.tm_sec == 0)) { warOffline = offline; seiteZeichnen(); }
    lichtStellen();
  }
  // Striche zeichnet stricheTask
  if (jetzt - letztesLaufen > 60000) {
    if (!letztesLaufen) startBestaetigen();       // eine Minute ohne Absturz: neue Fassung gilt
    letztesLaufen = jetzt; Serial.println("laeuft");
  }
  delay(2);
}
