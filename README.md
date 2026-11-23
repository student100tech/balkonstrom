# Balkonstrom

Android-App für Zendure-Balkonspeicher. Liest die Werte **direkt vom Gerät im
WLAN** – kein Zendure-Konto, kein Token, keine Cloud.

Unterstützt **SolarFlow 800 Plus** und **SolarFlow 800 Pro 2** (vier
Solareingänge, Off-Grid-Dose, Lüfter) sowie das **Smart Meter 3CT** als
eigenständiges Gerät im selben Netz.

## Was die App zeigt

- **Live** – Flussdiagramm mit Solar, Netz, Haus, Akku und Off-Grid-Dose.
  Die Striche laufen in der Farbe der speisenden Quelle, bis zu drei
  gleichzeitig. Am Akku stehen Ladestand, Leistung und Restzeit, die beiden
  Ladegrenzen liegen als rote und blaue Marke auf dem Ring.
- **Zustand** – der vollständige Gerätebericht in Themenkarten: Solar je
  Eingang, Akku, Zellen, Temperatur, Grenzen, Betrieb, Gerät.
- **Einstellungen** – Anzeige (Sprache deutsch/russisch, hell/dunkel,
  Schriftgröße), Verbindung, eigene Anlage, Selbstaktualisierung.

Dazu eine **Demo-Ansicht** mit sechs erfundenen Szenen, die ohne Gerät läuft,
und ein **Rohbericht**, der alle Felder ausgibt – gekürzt um Gerätenummern
und Namen.

Eine **Netzprüfung** sucht im WLAN nach allem, was antwortet: Zendure-Geräte
über HTTP, Fremdgeräte über Modbus TCP auf Port 502, und GoodWe-Dongles über
einen UDP-Rundruf auf Port 48899. Sie endet mit einem Satz im Klartext statt
mit einem Protokoll und ist teilbar. Der Knopf steht in den Einstellungen und
zusätzlich im Einrichtungsbildschirm – dort erreichbar auch dann, wenn gar
kein Zendure-Gerät eingetragen ist.

## Woher die Daten kommen

| Aufruf | Zweck |
|---|---|
| `GET http://<IP>/properties/report` | alle Live-Werte als JSON |
| `POST http://<IP>/properties/write` | Werte setzen |

Das Gerät antwortet jedem im selben WLAN. Abgefragt wird höchstens alle
3 Sekunden, so wie Zendure es empfiehlt. Gefunden wird es per mDNS
(`_zendure._tcp`), sonst durch Absuchen des Netzes.

Seit Build 31 erkennt die Suche zusätzlich **Modbus TCP auf Port 502** –
Vorarbeit für GoodWe ESA.

## Selbstaktualisierung

Die App prüft drei Sekunden nach dem Start die **Releases** dieses Repos und
setzt einen Punkt ans Zahnrad, wenn es etwas Neues gibt. Sie springt immer
direkt auf die jüngste Fassung; Zwischenstände müssen nicht installiert
werden. Der Signierschlüssel liegt fest im Repo, Updates lassen sich deshalb
über die alte Fassung installieren.

## Monitor flashen

Geplant ist ein Wandbild auf **ESP32-S3** (Guition 4848S040, 480 × 480), das
dasselbe Flussdiagramm zeigt. Das Flussdiagramm der App ist deshalb exakt
quadratisch (350 × 350) und passt 1:1 auf den Schirm.

Die Firmware wird **aus der App heraus über USB aufgespielt** – Board ans
Handy stecken, fertig. Danach holt sich der Monitor seine Updates selbst über
WLAN aus denselben Releases.

Der Bereich ist in den Einstellungen **hinter einem Code verborgen**, damit
ihn niemand versehentlich benutzt: fünfmal auf die Versionszeile tippen, dann
den Code eingeben.

## Schrift

Eine einzige Schrift für alles: **Manrope**, SIL Open Font License 1.1. Sie
liegt als `www/fonts/Manrope.woff` in der App, nicht im Netz — die App läuft
also ohne Internet. 29 KB, beschnitten auf ASCII, deutsche Umlaute und
Kyrillisch, variabler Schnitt 200–800.

Vorher liefen Inter und Space Grotesk über Google Fonts. Space Grotesk hat
kein einziges kyrillisches Zeichen, weshalb in der russischen Oberfläche die
Einheit „Вт" aus einer eingesprungenen Ersatzschrift kam.

## Aufbau

```
www/index.html     die ganze App, eine Datei
www/fonts/         Manrope samt OFL.txt
android-src/       Capacitor-Plugins (Gerätesuche, Update, Flashen) und MainActivity
shared/modell.json einzige Quelle für Farben, Umrechnungen, Schwellen, Strichtakt
tools/modell.py    trägt das Modell in index.html ein und erzeugt firmware/modell.h
firmware/          Programm für das ESP32-Wandbild
assets/            Symbol und Startbild
```

`shared/modell.json` ist der Grund, warum App und Wandbild nicht
auseinanderlaufen: Vorzeichenregeln, Temperaturformel, Schwellen, Farben und
der Strichtakt stehen genau einmal da. `tools/modell.py --pruefen` meldet mit
Fehlercode, wenn irgendwo wieder eine Zahl von Hand abweicht.

## Bauen

Jeder Push auf `main` baut über GitHub Actions eine neue APK und legt sie
unter **Releases** ab. Die Build-Nummer ist die Nummer des Arbeitslaufs.
