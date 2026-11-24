// PV-Monitor – Firmware fuer das Wandbild
// Board: Guition ESP32-S3-4848S040 (Teilenummer JC4848W540C), 480x480, ST7701S
//
// Dieser Stand ist das Geruest: er weckt das Display, prueft den Touch und zeigt,
// dass die Kette vom Bauablauf ueber die Freigabe bis zum Aufspielen vom Handy
// traegt. Die Bildschirme aus dem Entwurf (Fluss, Zustand, Verlauf, Einrichtung)
// kommen darauf.
//
// Pins und Zeitlagen stammen aus dem Arduino-Demo des Herstellers,
// 1-Demo/Demo_Arduino/1_2_4.0_LvglWidgets, nicht aus einem Datenblatt.

#include <Arduino_GFX_Library.h>
#include <Wire.h>

#ifndef FW_BUILD
#define FW_BUILD 0
#endif

static const int PIN_LICHT = 38;        // Hintergrundlicht, PWM
static const int PIN_SDA   = 19;        // I2C zum Touch GT911
static const int PIN_SCL   = 45;        // Achtung: Strapping-Pin
static const uint8_t GT911_ADDRS[2] = {0x5D, 0x14};

// Arduino_GFX 1.2.9 bringt kein Farbmakro mit, nur benannte Konstanten.
// 5 Bit Rot, 6 Bit Gruen, 5 Bit Blau. Das Panel laeuft als BGR, das dreht
// der Treiber selbst - hier stehen also gewoehnliche RGB-Werte.
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// Farben der App, damit Handy und Wandbild dieselbe Sprache sprechen
static const uint16_t C_GRUND  = RGB(0x0F, 0x14, 0x20);
static const uint16_t C_TEXT   = RGB(0xE8, 0xEA, 0xF0);
static const uint16_t C_MATT   = RGB(0x8A, 0x91, 0xA3);
static const uint16_t C_SOLAR  = RGB(0xFF, 0xB5, 0x47);
static const uint16_t C_AKKU   = RGB(0x3D, 0xDB, 0xB0);
static const uint16_t C_NETZ   = RGB(0xFF, 0x6B, 0x81);
static const uint16_t C_HAUS   = RGB(0xA7, 0x8B, 0xFA);

Arduino_ESP32RGBPanel *bus = new Arduino_ESP32RGBPanel(
  39 /* CS */, 48 /* SCK */, 47 /* SDA */,
  18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
  11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */,  0 /* R4 */,
   8 /* G0 */, 20 /* G1 */,  3 /* G2 */, 46 /* G3 */,  9 /* G4 */, 10 /* G5 */,
   4 /* B0 */,  5 /* B1 */,  6 /* B2 */,  7 /* B3 */, 15 /* B4 */);

// Zeitlagen wie im Herstellerdemo. Der Pixeltakt steht auf 16 MHz; daraus
// ergeben sich 548 x 518 Takte je Bild und damit rund 56 Bilder je Sekunde.
Arduino_ST7701_RGBPanel *gfx = new Arduino_ST7701_RGBPanel(
  bus, GFX_NOT_DEFINED /* RST */, 0 /* Drehung */,
  true /* IPS */, 480, 480,
  st7701_type1_init_operations, sizeof(st7701_type1_init_operations),
  true /* BGR */,
  10 /* hsync front porch */, 8 /* hsync pulse */, 50 /* hsync back porch */,
  10 /* vsync front porch */, 8 /* vsync pulse */, 20 /* vsync back porch */);

static uint8_t touchAdr = 0;            // 0 = keiner gefunden

static uint8_t sucheTouch() {
  for (uint8_t i = 0; i < 2; i++) {
    Wire.beginTransmission(GT911_ADDRS[i]);
    if (Wire.endTransmission() == 0) return GT911_ADDRS[i];
  }
  return 0;
}

// GT911: Register 0x814E traegt im unteren Nibble die Zahl der Beruehrungen,
// ab 0x8150 folgen je acht Byte mit Kennung, X und Y. Nach dem Lesen muss das
// Statusregister geloescht werden, sonst meldet der Baustein nichts Neues.
static bool liesTouch(uint16_t &x, uint16_t &y) {
  if (!touchAdr) return false;
  Wire.beginTransmission(touchAdr);
  Wire.write(0x81); Wire.write(0x4E);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)touchAdr, 1) != 1) return false;
  uint8_t st = Wire.read();
  bool treffer = false;
  if ((st & 0x80) && (st & 0x0F)) {
    Wire.beginTransmission(touchAdr);
    Wire.write(0x81); Wire.write(0x50);
    if (Wire.endTransmission() == 0 && Wire.requestFrom((int)touchAdr, 6) == 6) {
      Wire.read();                                  // Kennung des Fingers
      uint8_t xl = Wire.read(), xh = Wire.read();
      uint8_t yl = Wire.read(), yh = Wire.read();
      x = xl | (xh << 8); y = yl | (yh << 8);
      treffer = true;
    }
  }
  if (st & 0x80) {                                  // Status zuruecksetzen
    Wire.beginTransmission(touchAdr);
    Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
    Wire.endTransmission();
  }
  return treffer;
}

static void mitte(const char *s, int y, uint16_t farbe, uint8_t groesse) {
  gfx->setTextSize(groesse);
  gfx->setTextColor(farbe);
  int16_t x1, y1; uint16_t w, h;
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  gfx->setCursor((480 - w) / 2, y);
  gfx->print(s);
}

// Die Sonne des App-Icons, hier in Bildpunkten: Scheibe mit zehn Strahlen,
// Strahl von 1,613 bis 2,085 Radien, Strich 0,283 Radien - dieselben Masse
// wie im Tagesbogen der App.
static void sonne(int cx, int cy, int r) {
  gfx->fillCircle(cx, cy, r, C_SOLAR);
  const float innen = r * 1.613f, aussen = r * 2.085f;
  const int dicke = max(2, (int)lroundf(r * 0.283f));
  for (int k = 0; k < 10; k++) {
    float w = (k * 36 - 90) * 0.017453293f;
    int x1 = cx + lroundf(innen  * cosf(w)), y1 = cy + lroundf(innen  * sinf(w));
    int x2 = cx + lroundf(aussen * cosf(w)), y2 = cy + lroundf(aussen * sinf(w));
    for (int d = -(dicke / 2); d <= dicke / 2; d++)
      gfx->drawLine(x1 + d, y1, x2 + d, y2, C_SOLAR);
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.printf("PV-Monitor Firmware Build %d\n", FW_BUILD);

  gfx->begin(16000000);
  gfx->fillScreen(C_GRUND);

  // Hintergrundlicht genau wie im Herstellerdemo: einfach einschalten.
  // Build 4 steuerte es per PWM (150 Hz) - das Board blieb danach schwarz.
  // Dimmen kommt zurueck, sobald ein Bild steht.
  pinMode(PIN_LICHT, OUTPUT);
  digitalWrite(PIN_LICHT, HIGH);

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  touchAdr = sucheTouch();
  Serial.printf("Touch: %s\n", touchAdr ? "gefunden" : "nicht gefunden");

  sonne(240, 150, 34);
  mitte("PV-Monitor", 230, C_TEXT, 4);

  char z[48];
  snprintf(z, sizeof z, "Firmware Build %d", FW_BUILD);
  mitte(z, 272, C_MATT, 2);
  mitte(touchAdr ? "Touch bereit - Bildschirm beruehren" : "Touch nicht gefunden",
        300, touchAdr ? C_MATT : C_NETZ, 1);

  // Vier Farbfelder: zeigt, dass die Farbkanaele richtig verdrahtet sind.
  // Waeren Rot und Blau vertauscht, faellt es hier sofort auf.
  const uint16_t f[4] = {C_SOLAR, C_AKKU, C_NETZ, C_HAUS};
  const char *n[4] = {"Solar", "Akku", "Netz", "Haus"};
  for (int i = 0; i < 4; i++) {
    int x = 40 + i * 105;
    gfx->fillRoundRect(x, 340, 90, 60, 14, f[i]);
    gfx->setTextSize(1); gfx->setTextColor(C_MATT);
    int16_t x1, y1; uint16_t w, h;
    gfx->getTextBounds(n[i], 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor(x + (90 - w) / 2, 412);
    gfx->print(n[i]);
  }
}

void loop() {
  static uint32_t letzte = 0;
  uint16_t x, y;
  if (liesTouch(x, y)) {
    gfx->fillCircle(x, y, 10, C_AKKU);
    char z[40];
    snprintf(z, sizeof z, "%3u / %3u    ", x, y);
    gfx->fillRect(150, 440, 180, 20, C_GRUND);
    gfx->setTextSize(2); gfx->setTextColor(C_TEXT);
    gfx->setCursor(160, 442);
    gfx->print(z);
  }
  // Lebenszeichen auf der seriellen Schnittstelle, damit sich ein haengender
  // Start von einem stillen Display unterscheiden laesst.
  if (millis() - letzte > 5000) { letzte = millis(); Serial.println("laeuft"); }
  delay(16);
}
