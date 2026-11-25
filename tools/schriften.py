#!/usr/bin/env python3
"""Erzeugt firmware/pvmonitor/schriften.h aus Manrope.

Jede Schrift ist eine feste Kombination aus Stärke und Pixelgröße, genau die,
die die Monitor-Bildschirme brauchen (App-Wert mal 480/350 für das Diagramm,
Entwurfswerte für die übrigen Seiten). Die Glyphen werden mit 16 Graustufen
abgelegt, damit Kanten weich bleiben - mit 1 Bit sähe Manrope ausgefranst aus.

Für Zahlen wie font-variant-numeric: tabular-nums in der App steht je Schrift
die breiteste Ziffer dabei; die Firmware setzt Ziffern dann auf diese Breite,
damit Werte beim Aktualisieren nicht wackeln. Namen bleiben proportional.

Aufruf: python3 tools/schriften.py <Ordner mit Manrope-*.ttf>
"""
import sys, pathlib
from PIL import Image, ImageDraw, ImageFont

ORDNER = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'Manrope/static')
ZIEL = pathlib.Path(__file__).resolve().parent.parent / 'firmware' / 'pvmonitor' / 'schriften.h'

STAERKE = {'R': 'Regular', 'M': 'Medium', 'S': 'SemiBold', 'B': 'Bold'}
# Name -> (Stärke, Größe in px)
SCHRIFTEN = {
    'R105': ('R', 10.5), 'R115': ('R', 11.5), 'R120': ('R', 12), 'R125': ('R', 12.5),
    'R130': ('R', 13), 'R135': ('R', 13.5), 'R140': ('R', 14), 'R150': ('R', 15),
    'R160': ('R', 16), 'R165': ('R', 16.5), 'R171': ('R', 17.1), 'R180': ('R', 18),
    'R190': ('R', 19), 'R200': ('R', 20),
    'M125': ('M', 12.5), 'M142': ('M', 14.2), 'M171': ('M', 17.1), 'M178': ('M', 17.8),
    'S150': ('S', 15), 'S160': ('S', 16), 'S180': ('S', 18), 'S199': ('S', 19.9),
    'S200': ('S', 20), 'S220': ('S', 22), 'S233': ('S', 23.3), 'S261': ('S', 26.1),
    'B219': ('B', 21.9),
}
# Nach Codepunkt sortiert - die Firmware sucht binär.
ZEICHEN = sorted(set([chr(c) for c in range(32, 127)] + list('ÄÖÜäöüß°·–—−→…•²€')
                     + [chr(c) for c in range(0x410, 0x450)] + ['Ё', 'ё']))


def glyphe(font, ch):
    l, t, r, b = font.getbbox(ch, anchor='ls')
    vorschub = font.getlength(ch)
    w, h = max(0, r - l), max(0, b - t)
    if w == 0 or h == 0:
        return 0, 0, 0, 0, vorschub, []
    bild = Image.new('L', (w, h), 0)
    ImageDraw.Draw(bild).text((-l, -t), ch, font=font, fill=255, anchor='ls')
    px = [(v + 8) // 17 for v in bild.tobytes()]          # 0..255 -> 0..15
    return w, h, l, t, vorschub, px


def main():
    teile = ['// Erzeugt von tools/schriften.py aus Manrope (SIL Open Font License) -',
             '// nicht von Hand ändern.', '#pragma once', '#include "schrift.h"', '']
    gesamt = 0
    for name, (st, groesse) in SCHRIFTEN.items():
        font = ImageFont.truetype(str(ORDNER / f'Manrope-{STAERKE[st]}.ttf'), groesse)
        daten, eintraege = [], []
        ziffer = max(font.getlength(str(d)) for d in range(10))
        for ch in ZEICHEN:
            w, h, xo, yo, vor, px = glyphe(font, ch)
            # Manrope hat ein sehr schmales Leerzeichen (0,2 em). Auf dem Glas und
            # aus Abstand liefen "PV1 620 W" so zu "PV1620 W" zusammen - etwas breiter.
            if ch == ' ': vor *= 1.3
            ofs = len(daten) * 2
            if len(px) % 2: px.append(0)
            daten += [(px[i] << 4) | px[i + 1] for i in range(0, len(px), 2)]
            eintraege.append(f'{{0x{ord(ch):04X},{ofs},{w},{h},{xo},{yo},{round(vor * 16)}}}')
        asc, desc = font.getmetrics()
        gross = -font.getbbox('H', anchor='ls')[1]
        teile.append(f'static const uint8_t {name}_px[] = {{' +
                     ','.join(str(b) for b in daten) + '};')
        teile.append(f'static const Glyphe {name}_gl[] = {{' + ','.join(eintraege) + '};')
        teile.append(f'static const Schrift {name} = {{{name}_gl, {len(eintraege)}, {name}_px, '
                     f'{asc}, {desc}, {gross}, {round(ziffer * 16)}}};')
        teile.append('')
        gesamt += len(daten) + len(eintraege) * 12
    ZIEL.write_text('\n'.join(teile) + '\n', encoding='utf-8')
    print(f'{len(SCHRIFTEN)} Schriften, {len(ZEICHEN)} Zeichen je Schrift, '
          f'rund {gesamt // 1024} KB -> {ZIEL}')


main()
