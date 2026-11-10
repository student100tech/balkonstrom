# Balkonstrom Live

Android-App für den Zendure SolarFlow 800 Plus.

- **Live:** Werte direkt vom Gerät im WLAN (zenSDK, `http://<IP>/properties/report`)
- **Statistik:** Tageswerte der letzten 7 Tage aus der Zendure-Cloud (Login in den Einstellungen)
- **Steuerung:** Ausgangsleistung, Lade-/Entladegrenze, Laden aus dem Netz (`/properties/write`)

Jeder Push auf `main` baut automatisch eine neue APK → **Releases**.

App-Code: `www/index.html` · Icon: `assets/`
