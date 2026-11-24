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

// Einbaulage: der Halter dreht das Board um 90 Grad im Uhrzeigersinn, die
// USB-Buchse zeigt nach unten. Arduino_GFX 1.2.9 kann bei diesem Panel nur 0
// und 180 Grad ("case 1/3: not implemented"), also drehen wir selbst. Weil das
// Display quadratisch ist, reicht es, die vier Grundfunktionen umzurechnen -
// Text, Kreise und Linien laufen alle darueber.
//
//   Panel (px, py) = (y, 479 - x)     fuer einen Punkt (x, y) im Bild
//
// Herleitung: nach der Drehung ist die linke Panelkante oben (px = y) und die
// obere Panelkante rechts (py = 479 - x).
class GedrehtesPanel : public Arduino_ST7701_RGBPanel {
public:
  using Arduino_ST7701_RGBPanel::Arduino_ST7701_RGBPanel;
  static const int16_t N = 479;
  void writePixelPreclipped(int16_t x, int16_t y, uint16_t c) override {
    Arduino_ST7701_RGBPanel::writePixelPreclipped(y, N - x, c);
  }
  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override {
    Arduino_ST7701_RGBPanel::writeFastVLine(y, N - x - w + 1, w, c);
  }
  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override {
    Arduino_ST7701_RGBPanel::writeFastHLine(y, N - x, h, c);
  }
  void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override {
    Arduino_ST7701_RGBPanel::writeFillRectPreclipped(y, N - x - w + 1, h, w, c);
  }
};

// Zeitlagen wie im Herstellerdemo. Der Pixeltakt steht auf 16 MHz; daraus
// ergeben sich 548 x 518 Takte je Bild und damit rund 56 Bilder je Sekunde.
GedrehtesPanel *gfx = new GedrehtesPanel(
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
    // Ab 0x8150 stehen direkt X und Y des ersten Fingers; die Kennung liegt
    // davor auf 0x814F. Build 6 las eine Kennung zu viel und war damit um ein
    // Byte verrutscht - daher Werte wie 30209 auf einem 480er-Display.
    Wire.beginTransmission(touchAdr);
    Wire.write(0x81); Wire.write(0x50);
    if (Wire.endTransmission() == 0 && Wire.requestFrom((int)touchAdr, 4) == 4) {
      uint8_t xl = Wire.read(), xh = Wire.read();
      uint8_t yl = Wire.read(), yh = Wire.read();
      uint16_t px = xl | (xh << 8), py = yl | (yh << 8);
      // Der Touch meldet Panelkoordinaten; zurueck ins gedrehte Bild:
      // aus Panel = (y, 479 - x) folgt x = 479 - py, y = px.
      x = 479 - py; y = px;
      treffer = px < 480 && py < 480;
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
  // Farbumkehr AN. Die Bibliothek schickt bei IPS = true nach dem Start
  // "Umkehr aus" (0x20); auf diesem Panel waren damit Hell und Dunkel
  // vertauscht - Build 6 zeigte hellen Grund und blaue Sonne. In der
  // Startfolge des Herstellers steht 0x21 auskommentiert.
  gfx->invertDisplay(true);
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

  // Fuenf reine Testfarben mit Namen. Fotos taugen nicht zum Pruefen - die
  // Kamera hatte bei Build 6 den Rotanteil fast ganz weggefiltert. Was man
  // mit eigenen Augen sieht, ist eindeutig.
  const uint16_t f[5] = {RGB(255,0,0), RGB(0,255,0), RGB(0,0,255), RGB(255,255,255), RGB(0,0,0)};
  const char *n[5] = {"Rot", "Gruen", "Blau", "Weiss", "Schwarz"};
  for (int i = 0; i < 5; i++) {
    int x = 16 + i * 92;
    gfx->fillRoundRect(x, 330, 80, 60, 12, f[i]);
    gfx->drawRoundRect(x, 330, 80, 60, 12, C_MATT);   // damit Schwarz sichtbar bleibt
    gfx->setTextSize(2); gfx->setTextColor(C_TEXT);
    int16_t x1, y1; uint16_t w, h;
    gfx->getTextBounds(n[i], 0, 0, &x1, &y1, &w, &h);
    gfx->setCursor(x + (80 - (int)w) / 2, 400);
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
    Serial.printf("Touch %u/%u\n", x, y);
  }
  // Lebenszeichen auf der seriellen Schnittstelle, damit sich ein haengender
  // Start von einem stillen Display unterscheiden laesst.
  if (millis() - letzte > 5000) { letzte = millis(); Serial.println("laeuft"); }
  delay(16);
}
