#!/usr/bin/env python3
"""
Traegt shared/modell.json in alle Ziele ein, die es brauchen.

  www/index.html      zwischen den Marken MODELL-ANFANG / MODELL-ENDE
  firmware/modell.h   erzeugt, fuer das ESP32-Wandbild

Aufruf ohne Argumente schreibt. Mit --pruefen wird nur verglichen und bei
Abweichung mit Rueckgabewert 1 beendet – so faellt im Bauablauf auf, wenn
jemand eine Zahl von Hand geaendert hat, statt sie hier zu pflegen.
"""
import json, pathlib, re, sys

WURZEL = pathlib.Path(__file__).resolve().parent.parent
M = json.loads((WURZEL / "shared" / "modell.json").read_text(encoding="utf-8"))

ANFANG, ENDE = "/* MODELL-ANFANG", "/* MODELL-ENDE */"


def ohne_hinweise(x):
    if isinstance(x, dict):
        return {k: ohne_hinweise(v) for k, v in x.items() if k != "_hinweis"}
    if isinstance(x, list):
        return [ohne_hinweise(v) for v in x]
    return x


def js_block():
    d = ohne_hinweise(M)
    f = M["fluss"]
    return (
        ANFANG + " – erzeugt aus shared/modell.json, nicht von Hand ändern */\n"
        "  const MODELL = " + json.dumps(d, ensure_ascii=False, indent=2).replace("\n", "\n  ") + ";\n"
        "  const LAUF = MODELL.fluss.dauerMs, STRECKE = MODELL.fluss.periode;\n"
        "  " + ENDE
    )


def css_block():
    f = M["fluss"]
    return (
        "  .wire{stroke:transparent;stroke-width:%d;fill:none;stroke-linecap:round}\n"
        "  .wire.on{stroke-dasharray:%d %d}"
        % (f["strichbreite"], f["strichlaenge"], f["periode"] - f["strichlaenge"])
    )


def header():
    f, u, s, g, mb = M["fluss"], M["umrechnung"], M["schwellen"], M["geraete"], M["modbus"]
    z = []
    z.append("// Erzeugt aus shared/modell.json – nicht von Hand ändern.")
    z.append("#pragma once")
    z.append("")
    z.append("// Strichmuster der Energieflüsse")
    for k, v in f.items():
        if k.startswith("_"): continue
        z.append(f"static constexpr int FLUSS_{k.upper()} = {v};")
    z.append("")
    z.append("// Umrechnung der Rohwerte")
    for k, v in u.items():
        if k.startswith("_"): continue
        z.append(f"static constexpr int UM_{k.upper()} = {v};")
    z.append("")
    z.append("// Schwellen")
    for k, v in s.items():
        if k.startswith("_"): continue
        z.append(f"static constexpr int SCHWELLE_{k.upper()} = {v};")
    z.append("")
    z.append("// Farben (0xRRGGBB)")
    for thema, farben in M["farben"].items():
        for name, hexw in farben.items():
            z.append(f"static constexpr unsigned FARBE_{thema.upper()}_{name.upper()} = 0x{hexw[1:]};")
    z.append("")
    z.append("// Eingangsgrenze je MPPT nach Modell")
    for name, w in g["mpptMaxWatt"].items():
        z.append(f"// {name}: {w} W")
    z.append("")
    z.append(f'static constexpr const char* ZENDURE_PFAD = "{M["zendure"]["pfadBericht"]}";')
    z.append(f'static constexpr int ZENDURE_ABSTAND_S = {M["zendure"]["mindestabstandSekunden"]};')
    z.append(f'static constexpr int MODBUS_PORT = {mb["port"]};')
    return "\n".join(z) + "\n"


def ersetze(text, anfang, ende, neu):
    i, j = text.find(anfang), text.find(ende)
    if i < 0 or j < 0:
        raise SystemExit(f"Marken {anfang!r} / {ende!r} fehlen in der Datei.")
    return text[:i] + neu + text[j + len(ende):]


def main():
    pruefen = "--pruefen" in sys.argv
    abweichung = False

    html = WURZEL / "www" / "index.html"
    alt = html.read_text(encoding="utf-8")
    neu = ersetze(alt, ANFANG, ENDE, js_block())
    neu = ersetze(neu, "/* CSS-MODELL-ANFANG", "/* CSS-MODELL-ENDE */",
                  "/* CSS-MODELL-ANFANG – erzeugt aus shared/modell.json */\n"
                  + css_block() + "\n  /* CSS-MODELL-ENDE */")
    if neu != alt:
        abweichung = True
        if not pruefen:
            html.write_text(neu, encoding="utf-8"); print("www/index.html aktualisiert")

    h = WURZEL / "firmware" / "modell.h"
    h.parent.mkdir(exist_ok=True)
    inhalt = header()
    if not h.exists() or h.read_text(encoding="utf-8") != inhalt:
        abweichung = True
        if not pruefen:
            h.write_text(inhalt, encoding="utf-8"); print("firmware/modell.h erzeugt")

    if pruefen and abweichung:
        raise SystemExit("Abweichung: shared/modell.json und die Ziele passen nicht zusammen. "
                         "tools/modell.py ausführen und mit einchecken.")
    if not abweichung:
        print("alles aktuell")


main()
