# Prüfbild für den Flasher

`4.0_LvglWidgets.bin` ist **nicht unsere Firmware**. Es ist die
LVGL-Widgets-Demo, die der Hersteller des Boards
(Guition ESP32-S3-4848S040, interne Teilenummer JC4848W540C) seinem
Paket beilegt, unter `8-Burn operation/Burn files`.

Sie liegt hier aus einem einzigen Grund: Der Aufspielvorgang in der App
(Einstellungen → hinter dem Code → „Monitor flashen") braucht eine Datei,
die er aufs Board schreiben kann, und solange unsere eigene Firmware nicht
existiert, gibt es keine. Mit einem bekannten, funktionierenden Abbild
lässt sich die Kette trotzdem prüfen: USB-Erkennung, CH340, Reset über
DTR/RTS, das Übertragungsprotokoll und die MD5-Gegenprobe.

Geprüft: gültiges ESP-Abbild (erstes Byte 0xE9), Chip laut Kopf ESP32-S3,
3 Segmente, 652.640 Byte, MD5 `6229383d24643fae6f170bc308dad2e8`.
Adresse beim Schreiben: 0x0.

**Wenn unsere Firmware fertig ist, fliegt diese Datei hier raus** – sonst
lägen zwei `.bin` in einer Freigabe, und die App nimmt die erstbeste.

Zurück zum Werkszustand geht es mit `JC4848W540C_I_W-V2.1-NEWUI.bin` aus
demselben Herstellerpaket, ebenfalls auf 0x0. Die Datei liegt nicht hier,
sie ist 4,8 MB groß.
