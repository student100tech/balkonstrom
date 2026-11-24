#pragma once
#include <stdint.h>

// Eine Glyphe mit 16 Graustufen, zwei Pixel je Byte, Zeile für Zeile.
// xo/yo: Lage der linken oberen Ecke zur Grundlinie, vor: Vorschub in 1/16 px.
struct Glyphe { uint16_t cp; uint32_t ofs; uint8_t w, h; int8_t xo, yo; uint16_t vor; };
// ziffer: Vorschub der breitesten Ziffer in 1/16 px, für Zahlen in fester Breite
struct Schrift { const Glyphe *gl; uint16_t n; const uint8_t *px; uint8_t asc, desc, gross; uint16_t ziffer; };
