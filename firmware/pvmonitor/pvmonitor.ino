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
#include <time.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include "esp_ota_ops.h"
#include "seiten.h"
#include "zertifikate.h"

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
// aber verdreht - der fast schwarze Grund wurde orange (Build 9). Deshalb auch
// kein invertDisplay(true) mehr: Das war in Build 7 nur das Gegenmittel zur
// falschen Startfolge.
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

static uint16_t *fb = nullptr, *basisBuf = nullptr, *teilBuf = nullptr;
static Leinwand basis;
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
  prefs.putString("ip", ein.ip); prefs.putString("host", ein.host); prefs.putString("prod", ein.produkt);
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
static int slotAkt = -1; static float sumS = 0, sumH = 0, sumL = 0; static int sumN = 0;
static uint32_t letzteBuchung = 0, letzteSicherung = 0;

static void verlaufLaden() {
  Preferences prefs;   // je Aufruf eigenes Objekt - beide Kerne sichern
  prefs.begin("verlauf", true);
  if (prefs.getBytesLength("tag") == sizeof(Verlauf)) prefs.getBytes("tag", &verl, sizeof(Verlauf));
  else verl.leeren(0);
  prefs.end();
}
static void verlaufSichern() {
  Preferences prefs;   // je Aufruf eigenes Objekt - beide Kerne sichern
  prefs.begin("verlauf", false);
  prefs.putBytes("tag", &verl, sizeof(Verlauf));
  prefs.end();
}
static void verlaufBuchen(const Werte &w) {
  time_t jetzt = time(nullptr); struct tm t; localtime_r(&jetzt, &t);
  if (t.tm_year < 124) return;                       // Uhr noch nicht gestellt
  uint32_t tag = (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;
  uint32_t ms = millis();
  sperren();
  if (verl.tag != tag) {
    if (verl.tag) verlaufSichern();                  // gestern noch festhalten
    verl.leeren(tag); slotAkt = -1;
  }
  int slot = (t.tm_hour * 60 + t.tm_min) / 5;
  if (slot != slotAkt) { slotAkt = slot; sumS = sumH = sumL = 0; sumN = 0; }
  float solar = w.n(F_SOLAR), haus = w.n(F_HOME), laden = w.n(F_LADEN), netz = w.n(F_GRID);
  sumS += solar; sumH += haus; sumL += laden; sumN++;
  verl.solar[slot] = (int16_t)lroundf(sumS / sumN);
  verl.haus[slot] = (int16_t)lroundf(sumH / sumN);
  verl.laden[slot] = (int16_t)lroundf(sumL / sumN);
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

static void sucheSpeicher() {
  mdnsStart();
  Fund tmp[6]; int anz = 0;
  int n = mdnsLaeuft ? MDNS.queryService("zendure", "tcp") : 0;
  for (int i = 0; i < n && anz < 6; i++) {
    Fund &f = tmp[anz];
    snprintf(f.host, sizeof f.host, "%s", MDNS.hostname(i).c_str());
    snprintf(f.ip, sizeof f.ip, "%s", MDNS.IP(i).toString().c_str());
    f.produkt[0] = 0;
    // Nur Speicher übernehmen: ein Smart Meter antwortet ohne "properties"
    static Werte probe;
    if (holeBericht(f.ip, probe, 2500)) { snprintf(f.produkt, sizeof f.produkt, "%s", probe.produkt); anz++; }
  }
  sperren();
  memcpy(lage.funde, tmp, sizeof(Fund) * anz); lage.fundAnz = anz; lage.sucheLaeuft = false;
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
  sperren(); lage.updGeprueft = ok; if (ok) lage.updBuild = neuestes; freigeben();
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
      Serial.println("WLAN: verbunden");
      mdnsStart();
      a |= A_ZEIT;
    }
    warVerbunden = verbunden;
    if ((a & A_ZEIT) && verbunden) {
      configTzTime(ZONEN[ein.zone].tz, "pool.ntp.org", "time.cloudflare.com", "time.google.com");
      zeitAngestossen = true;
    }
    if (zeitAngestossen && !zeitGemeldet && zeitGestellt()) { zeitGemeldet = true; Serial.println("Zeit: gestellt"); }
    static uint32_t letztePruefung = 0; static bool erstePruefung = true;
    if (verbunden && (erstePruefung ? zeitGestellt() : millis() - letztePruefung > 6UL * 3600 * 1000)) a |= A_UPD_PRUEFEN;
    if (verbunden && (a & A_UPD_PRUEFEN)) { erstePruefung = false; letztePruefung = millis(); updatePruefen(); }
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
    if (verbunden && ip[0] && millis() - letzteAbfrage >= ZENDURE_ABSTAND_S * 1000UL) {
      letzteAbfrage = millis();
      static Werte w;
      if (holeBericht(ip, w, 2500)) {
        if (ersteAntwort || fehler >= 5) Serial.println("Speicher: Antwort ok");
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
static void allesZeigen() {
  memcpy(fb, basisBuf, 480 * 480 * 2);
  Cache_WriteBack_Addr((uint32_t)fb, 480 * 480 * 2);
}
static void teilZeigen(Leinwand &q, int x, int y, int w, int h) {
  for (int xx = x; xx < x + w; xx++) memcpy(fb + (479 - xx) * 480 + y, q.an(xx, y), h * 2);
  Cache_WriteBack_Addr((uint32_t)(fb + (479 - (x + w - 1)) * 480), w * 480 * 2);
}

// Striche: erst alle Drähte fertig in den Teilspeicher rechnen, dann in einem
// Zug kopieren - so fehlen sie auch beim Neuzeichnen der Seite nie sichtbar.
struct Teil { Leinwand L; int x, y, w, h; };
static Teil teile[8]; static int teilAnz = 0;
static void stricheRechnen(uint32_t ms) {
  teilAnz = 0; int belegt = 0;
  for (int i = 0; i < ui.drahtAnz; i++) {
    Teil &t = teile[teilAnz];
    Oberflaeche::drahtRahmen(ui.draht[i], t.x, t.y, t.w, t.h);
    if (belegt + t.w * t.h > TEIL_PX) break;
    t.L.init(teilBuf + belegt, t.x, t.y, t.w, t.h);
    belegt += t.w * t.h;
    for (int xx = t.x; xx < t.x + t.w; xx++) memcpy(t.L.an(xx, t.y), basis.an(xx, t.y), t.h * 2);
    Oberflaeche::striche(t.L, ui.draht[i], ms);
    teilAnz++;
  }
}
static void stricheZeigen() { for (int i = 0; i < teilAnz; i++) teilZeigen(teile[i].L, teile[i].x, teile[i].y, teile[i].w, teile[i].h); }

static Lage zeichenLage;
static void lageHolen() {
  sperren();
  zeichenLage = lage;
  if (ui.seite == S_VERLAUF) verlUI = verl;
  freigeben();
  uint32_t ok = letzteOk;
  zeichenLage.alterS = ok ? (int)((millis() - ok) / 1000) : -1;
  time_t j = time(nullptr); struct tm t; localtime_r(&j, &t);
  zeichenLage.uhr = t.tm_year >= 124;
  zeichenLage.hh = t.tm_hour; zeichenLage.mm = t.tm_min; zeichenLage.ss = t.tm_sec;
  zeichenLage.build = FW_BUILD;
  zeichenLage.verlauf = &verlUI;
}
static void seiteZeichnen() {
  lageHolen();
  basis.volleFlaeche();
  ui.zeichne(basis, zeichenLage);
  stricheRechnen(millis());
  allesZeigen();
  stricheZeigen();
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
  gfx->begin(16000000);       // schickt danach selbst 0x20, wie im Herstellerdemo
  fb = gfx->getFramebuffer();
  basisBuf = (uint16_t *)ps_malloc(480 * 480 * 2);
  teilBuf = (uint16_t *)heap_caps_malloc(TEIL_PX * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!basisBuf || !teilBuf || !fb) { Serial.println("Speicher fuer Bild fehlt"); }
  basis.init(basisBuf, 0, 0, 480, 480);

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  touchAdr = sucheTouch();
  Serial.println(touchAdr ? "Touch: gefunden" : "Touch: nicht gefunden");

  einstLaden();
  verlaufLaden();
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
}

static bool unten = false, lang = false, schlucken = false;
static int tx0, ty0, txl, tyl; static uint32_t t0 = 0, tZuletzt = 0;
static uint32_t letzteAnim = 0, letzteSekunde = 0, letzterTouch = 0, letztesLaufen = 0;
static int letzteSek = -1;

void loop() {
  uint32_t jetzt = millis();

  // ---- Touch: Tippen, Wischen, lang Drücken ----
  if (jetzt - letzterTouch >= 15) {
    letzterTouch = jetzt;
    int x, y, st = liesTouch(x, y);
    if (st == 1) {
      tZuletzt = jetzt;
      if (!unten) {
        unten = true; lang = false; tx0 = txl = x; ty0 = tyl = y; t0 = jetzt;
        schlucken = lichtJetzt < 100;          // nachts weckt die erste Berührung nur das Licht
        letzteBeruehrung = jetzt; lichtStellen();
      } else { txl = x; tyl = y; }
    }
    if (unten && (st == 0 || jetzt - tZuletzt > 150)) {
      unten = false;
      int dx = txl - tx0, dy = tyl - ty0;
      if (!lang && !schlucken) {
        if (abs(dx) > 60 && abs(dx) > abs(dy) * 3 / 2) ui.wische(dx < 0 ? 1 : -1);
        else if (abs(dx) < 25 && abs(dy) < 25) ui.tippe(tx0, ty0, zeichenLage);
      }
    }
    if (unten && !lang && !schlucken && jetzt - t0 > 1500 && abs(txl - tx0) < 25 && abs(tyl - ty0) < 25) {
      lang = true; ui.lange();
    }
  }

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
    // Jede Sekunde nur die Uhr; der Verlauf bekommt einmal je Minute neue Punkte
    if (ui.seite == S_VERLAUF && tt.tm_sec == 0) seiteZeichnen();
    else {
      lageHolen();
      int x, y, pw, ph;
      if (ui.nurPille(basis, zeichenLage, x, y, pw, ph)) teilZeigen(basis, x, y, pw, ph);
    }
    lichtStellen();
  }
  if (ui.seite == S_FLUSS && ui.drahtAnz && jetzt - letzteAnim >= 33) {
    letzteAnim = jetzt;
    stricheRechnen(jetzt);
    stricheZeigen();
  }
  if (jetzt - letztesLaufen > 60000) {
    if (!letztesLaufen) startBestaetigen();       // eine Minute ohne Absturz: neue Fassung gilt
    letztesLaufen = jetzt; Serial.println("laeuft");
  }
  delay(2);
}
