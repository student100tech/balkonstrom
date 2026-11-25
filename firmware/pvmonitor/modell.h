// Erzeugt aus shared/modell.json – nicht von Hand ändern.
#pragma once

// Strichmuster der Energieflüsse
static constexpr int FLUSS_PERIODE = 24;
static constexpr int FLUSS_STRICHLAENGE = 4;
static constexpr int FLUSS_PLAETZE = 3;
static constexpr int FLUSS_DAUERMS = 1100;
static constexpr int FLUSS_STRICHBREITE = 3;
static constexpr int FLUSS_MINWATT = 2;

// Umrechnung der Rohwerte
static constexpr int UM_TEMPOFFSETZEHNTELKELVIN = 2731;
static constexpr int UM_TEMPTEILER = 10;
static constexpr int UM_SPANNUNGTEILER = 100;
static constexpr int UM_STROMTEILER = 10;
static constexpr int UM_SOCPROMILLETEILER = 10;
static constexpr int UM_VORZEICHEN16BITGRENZE = 32767;
static constexpr int UM_VORZEICHEN16BITABZUG = 65536;

// Schwellen
static constexpr int SCHWELLE_ZELLSPREIZUNGAUFFAELLIGMV = 50;
static constexpr int SCHWELLE_ZELLSPREIZUNGAUFFAELLIGMVUNTEN = 60;
static constexpr int SCHWELLE_LADESTANDUNTENPROZENT = 25;
static constexpr int SCHWELLE_FAULTLEVELSTOERUNGAB = 2;
static constexpr int SCHWELLE_PVTOTZONEWATTJEEINGANG = 10;

// Farben (0xRRGGBB)
static constexpr unsigned FARBE_HELL_SOLAR = 0xC98A00;
static constexpr unsigned FARBE_HELL_BAT = 0x0F8A64;
static constexpr unsigned FARBE_HELL_GRID = 0x2F6FB3;
static constexpr unsigned FARBE_HELL_HOME = 0x3D3A35;
static constexpr unsigned FARBE_DUNKEL_SOLAR = 0xE8B44C;
static constexpr unsigned FARBE_DUNKEL_BAT = 0x5ED3A5;
static constexpr unsigned FARBE_DUNKEL_GRID = 0x8FA3BF;
static constexpr unsigned FARBE_DUNKEL_HOME = 0xF2F4F8;

// Eingangsgrenze je MPPT nach Modell
// solarFlow800Pro2: 660 W
// solarFlow800Plus: 750 W
// solarFlow800: 600 W

static constexpr const char* ZENDURE_PFAD = "/properties/report";
static constexpr int ZENDURE_ABSTAND_S = 3;
static constexpr int MODBUS_PORT = 502;
