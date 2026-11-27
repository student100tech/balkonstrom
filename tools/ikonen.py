#!/usr/bin/env python3
"""Erzeugt firmware/pvmonitor/ikonen.h: die Symbole der App als Linienzüge.

Die Pfade stammen 1:1 aus www/index.html und den Monitor-Entwürfen. Bögen und
Kurven werden hier am PC in kurze Geraden zerlegt, damit das Board nur noch
Linien ziehen muss. Jeder Teil zwischen | ist ein eigener SVG-<path> und beginnt bei 0/0.
Format je Symbol:
  { Teile, n1, zu1, x, y, ..., n2, zu2, x, y, ... }
"""
import math, re, pathlib

ZIEL = pathlib.Path(__file__).resolve().parent.parent / 'firmware' / 'pvmonitor' / 'ikonen.h'

IKONEN = {
    # Lucide "solar-panel", 24er-Raster (App: translate(160 27) scale(1.25))
    'IK_SOLAR': 'M11 2h2|m14.28 14-4.56 8|m21 22-1.558-4H4.558|M3 10v2 '
                'M6.245 15.04A2 2 0 0 1 8 14h12a1 1 0 0 1 .864 1.505l-3.11 5.457A2 2 0 0 1 16 22H4a1 1 0 0 1-.863-1.506z '
                'M7 2a4 4 0 0 1-4 4|m8.66 7.66 1.41 1.41',
    # Lucide "house-plug" (App: translate(289 127) scale(1.25))
    'IK_HAUS': 'M10 12V8.964|M14 12V8.964 '
               'M15 12a1 1 0 0 1 1 1v2a2 2 0 0 1-2 2h-4a2 2 0 0 1-2-2v-2a1 1 0 0 1 1-1z '
               'M8.5 21H5a2 2 0 0 1-2-2v-9a2 2 0 0 1 .709-1.528l7-6a2 2 0 0 1 2.582 0l7 6A2 2 0 0 1 21 10v9a2 2 0 0 1-2 2h-5a2 2 0 0 1-2-2v-2',
    # Strommast, in App-Koordinaten
    'IK_MAST': 'M46 127 l-8 30|M46 127 l8 30|M37 135 h18|M40 145 h12',
    # Blitz in der Nabe, gefüllt, App-Koordinaten
    'IK_BLITZ': 'M179 128 l-11 16 h8 l-3 12 l11-16 h-8z',
    # Off-Grid-Dose, 24er-Raster (App: translate(29 237))
    'IK_DOSE': 'M9 2v5|M15 2v5|M5 7h14v4.5a7 7 0 0 1-14 0z|M12 18.5V22',
    # Heizung: drei senkrechte Wellen, 24er-Raster (App: translate(137 211) scale(.6))
    'IK_HEIZ': 'M7 3.5c-1.7 1.7-1.7 3.3 0 5c1.7 1.7 1.7 3.3 0 5c-1.7 1.7-1.7 3.3 0 5|M12 3.5c-1.7 1.7-1.7 3.3 0 5c1.7 1.7 1.7 3.3 0 5c-1.7 1.7-1.7 3.3 0 5|M17 3.5c-1.7 1.7-1.7 3.3 0 5c1.7 1.7 1.7 3.3 0 5c-1.7 1.7-1.7 3.3 0 5',
    # Tastatur
    'IK_UMSCHALT': 'M12 4 4 13h4v6h8v-6h4z',
    'IK_LOESCHEN': 'M20 6H9l-5 6 5 6h11a1 1 0 0 0 1-1V7a1 1 0 0 0-1-1z|m17 10-4 4|m13 10 4 4',
    'IK_EINGABE': 'M20 5v6a3 3 0 0 1-3 3H5|m9 10-4 4 4 4',
    'IK_HAKEN': 'M5 12.5 9.5 17 19 7',
    'IK_ZURUECK': 'm15 5-7 7 7 7',
    # Reiter, wie in der App (24er-Raster)
    'IK_FLUSS': 'M9.5 4.5a2.5 2.5 0 1 0 5 0a2.5 2.5 0 1 0 -5 0|m10.2 6.3-3.9 3.9|M2 12a2.5 2.5 0 1 0 5 0a2.5 2.5 0 1 0 -5 0|M7 12h10|M17 12a2.5 2.5 0 1 0 5 0a2.5 2.5 0 1 0 -5 0|m13.8 17.7 3.9-3.9|M9.5 19.5a2.5 2.5 0 1 0 5 0a2.5 2.5 0 1 0 -5 0',
    'IK_ZUSTAND': 'M3 12h4l3-8 4 16 3-8h4',
    'IK_VERLAUF': 'M3 3v16a2 2 0 0 0 2 2h16|m19 9-5 5-4-4-3 3',
    'IK_ZAHNRAD': 'M9 12a3 3 0 1 0 6 0a3 3 0 1 0 -6 0|M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z',
    # Logo der App (42er-Raster): Sonnenhälfte und Blitz gefüllt
    'IK_LOGO_SONNE': 'M16.47 21.3 A4.53 4.53 0 0 1 25.53 21.3 Z',
    'IK_LOGO_BLITZ': 'M21.6 24.62 L19.11 27.19 H21.0 L20.25 29.0 L22.81 26.28 H20.85 Z',
}


def zahlen(t):
    return [float(v) for v in re.findall(r'-?(?:\d+\.?\d*|\.\d+)(?:e-?\d+)?', t)]


def bogen(x0, y0, rx, ry, phi, fa, fs, x1, y1):
    # SVG-Endpunktform -> Mittelpunktform (SVG 1.1, Anhang F.6)
    if rx == 0 or ry == 0:
        return [(x1, y1)]
    c, s = math.cos(math.radians(phi)), math.sin(math.radians(phi))
    dx, dy = (x0 - x1) / 2, (y0 - y1) / 2
    x_, y_ = c * dx + s * dy, -s * dx + c * dy
    lam = (x_ ** 2) / rx ** 2 + (y_ ** 2) / ry ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx ** 2 * ry ** 2 - rx ** 2 * y_ ** 2 - ry ** 2 * x_ ** 2
    den = rx ** 2 * y_ ** 2 + ry ** 2 * x_ ** 2
    k = math.sqrt(max(0, num / den)) * (-1 if fa == fs else 1)
    cx_, cy_ = k * rx * y_ / ry, -k * ry * x_ / rx
    cx, cy = c * cx_ - s * cy_ + (x0 + x1) / 2, s * cx_ + c * cy_ + (y0 + y1) / 2
    def w(ux, uy, vx, vy):
        a = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
        return a
    t1 = w(1, 0, (x_ - cx_) / rx, (y_ - cy_) / ry)
    dt = w((x_ - cx_) / rx, (y_ - cy_) / ry, (-x_ - cx_) / rx, (-y_ - cy_) / ry)
    if not fs and dt > 0: dt -= 2 * math.pi
    if fs and dt < 0: dt += 2 * math.pi
    n = max(2, int(abs(dt) / (math.pi / 12)) + 1)
    pts = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        pts.append((cx + rx * math.cos(t) * c - ry * math.sin(t) * s,
                    cy + rx * math.cos(t) * s + ry * math.sin(t) * c))
    return pts


def pfad(d):
    teile, akt, x, y, sx, sy = [], None, 0.0, 0.0, 0.0, 0.0
    for cmd, args in re.findall(r'([MmLlHhVvAaCcZz])([^MmLlHhVvAaCcZz]*)', d):
        a = zahlen(args)
        if cmd in 'Zz':
            if akt: akt[1] = True
            x, y = sx, sy
            continue
        if cmd in 'Mm':
            for i in range(0, len(a), 2):
                nx, ny = (a[i], a[i + 1]) if cmd == 'M' else (x + a[i], y + a[i + 1])
                if i == 0:
                    akt = [[(nx, ny)], False]; teile.append(akt); sx, sy = nx, ny
                else:
                    akt[0].append((nx, ny))
                x, y = nx, ny
        elif cmd in 'Ll':
            for i in range(0, len(a), 2):
                x, y = (a[i], a[i + 1]) if cmd == 'L' else (x + a[i], y + a[i + 1])
                akt[0].append((x, y))
        elif cmd in 'Hh':
            for v in a:
                x = v if cmd == 'H' else x + v; akt[0].append((x, y))
        elif cmd in 'Vv':
            for v in a:
                y = v if cmd == 'V' else y + v; akt[0].append((x, y))
        elif cmd in 'Aa':
            for i in range(0, len(a), 7):
                rx, ry, phi, fa, fs, ex, ey = a[i:i + 7]
                if cmd == 'a': ex, ey = x + ex, y + ey
                akt[0] += bogen(x, y, rx, ry, phi, int(fa), int(fs), ex, ey)
                x, y = ex, ey
        elif cmd in 'Cc':
            for i in range(0, len(a), 6):
                p = a[i:i + 6]
                if cmd == 'c': p = [p[j] + (x if j % 2 == 0 else y) for j in range(6)]
                for k in range(1, 9):
                    t = k / 8
                    bx = (1-t)**3*x + 3*(1-t)**2*t*p[0] + 3*(1-t)*t*t*p[2] + t**3*p[4]
                    by = (1-t)**3*y + 3*(1-t)**2*t*p[1] + 3*(1-t)*t*t*p[3] + t**3*p[5]
                    akt[0].append((bx, by))
                x, y = p[4], p[5]
    return teile


def main():
    zeilen = ['// Erzeugt von tools/ikonen.py - nicht von Hand ändern.', '#pragma once', '']
    for name, d in IKONEN.items():
        teile = [t for teil in d.split('|') for t in pfad(teil)]
        werte = [len(teile)]
        for pts, zu in teile:
            if zu and len(pts) > 2 and pts[0] == pts[-1]: pts = pts[:-1]
            werte += [len(pts), 1 if zu else 0]
            for px, py in pts: werte += [round(px, 3), round(py, 3)]
        zeilen.append(f'static const float {name}[] = {{' + ','.join((f'{v:g}' + ('' if '.' in f'{v:g}' else '.') + 'f') if isinstance(v, float) else str(v) for v in werte) + '};')
    ZIEL.write_text('\n'.join(zeilen) + '\n', encoding='utf-8')
    print(f'{len(IKONEN)} Symbole -> {ZIEL}')


main()
