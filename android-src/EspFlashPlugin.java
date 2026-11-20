package de.elektroautonews.balkonstrom;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;
import android.os.Build;
import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;
import com.hoho.android.usbserial.driver.UsbSerialDriver;
import com.hoho.android.usbserial.driver.UsbSerialPort;
import com.hoho.android.usbserial.driver.UsbSerialProber;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Spielt eine Firmware ueber USB auf das ESP32-S3-Wandbild.
 *
 * Das Board haengt an einem CH340. Android kann als USB-Gastgeber arbeiten,
 * die Bibliothek usb-serial-for-android bedient den Wandler, und der
 * ROM-Bootloader des ESP32 nimmt die Daten entgegen - ein Stub-Loader wird
 * nicht gebraucht.
 *
 * Bewusst einfach gehalten: 115200 Baud, unkomprimiert, 1024 Byte je Block.
 * Das dauert fuer ein 2-MB-Abbild rund drei Minuten, dafuer gibt es keine
 * Baudraten-Umschaltung und keine Kompression, die schiefgehen kann. Das
 * Flashen passiert ein einziges Mal je Board.
 */
@CapacitorPlugin(name = "EspFlash")
public class EspFlashPlugin extends Plugin {

    /** Nur Dateien aus den Releases dieses Repos duerfen geschrieben werden. */
    private static final String ERLAUBT =
        "https://github.com/student100tech/balkonstrom/releases/download/";
    private static final String ACTION_USB = "de.elektroautonews.balkonstrom.USB_ERLAUBNIS";

    // ---- SLIP ----
    private static final byte SLIP_ENDE = (byte) 0xC0;
    private static final byte SLIP_ESC  = (byte) 0xDB;

    // ---- Befehle des ROM-Bootloaders ----
    private static final int CMD_FLASH_BEGIN = 0x02;
    private static final int CMD_FLASH_DATA  = 0x03;
    private static final int CMD_FLASH_END   = 0x04;
    private static final int CMD_SYNC        = 0x08;
    private static final int CMD_SPI_ATTACH  = 0x0D;
    private static final int CMD_FLASH_MD5   = 0x13;

    private static final int BLOCK = 1024;      // ROM nimmt hoechstens 1024 Byte
    private static final int BAUD  = 115200;

    // =====================================================================
    //  Oeffentliche Aufrufe
    // =====================================================================

    /** Sagt, ob ein Board steckt, und wie es heisst. */
    @PluginMethod
    public void check(PluginCall call) {
        JSObject o = new JSObject();
        try {
            UsbManager mgr = (UsbManager) getContext().getSystemService(Context.USB_SERVICE);
            List<UsbSerialDriver> gefunden = UsbSerialProber.getDefaultProber().findAllDrivers(mgr);
            if (gefunden.isEmpty()) {
                o.put("found", false);
            } else {
                UsbDevice d = gefunden.get(0).getDevice();
                String name = d.getProductName();
                if (name == null || name.isEmpty()) {
                    name = String.format("%04X:%04X", d.getVendorId(), d.getProductId());
                }
                o.put("found", true);
                o.put("name", name);
            }
        } catch (Throwable e) {
            o.put("found", false);
            o.put("error", String.valueOf(e.getMessage()));
        }
        call.resolve(o);
    }

    /** Laedt die Firmware und schreibt sie nach Adresse 0x0. */
    @PluginMethod
    public void flash(PluginCall call) {
        final String url = call.getString("url");
        if (url == null || !url.startsWith(ERLAUBT)) {
            call.reject("Unerlaubte Adresse");
            return;
        }
        new Thread(() -> {
            UsbSerialPort port = null;
            try {
                melde("open", 0);
                byte[] abbild = laden(url);

                UsbManager mgr = (UsbManager) getContext().getSystemService(Context.USB_SERVICE);
                List<UsbSerialDriver> gefunden = UsbSerialProber.getDefaultProber().findAllDrivers(mgr);
                if (gefunden.isEmpty()) throw new Exception("Kein Board gefunden");
                UsbSerialDriver treiber = gefunden.get(0);

                if (!mgr.hasPermission(treiber.getDevice()) && !erlaubnisHolen(mgr, treiber.getDevice())) {
                    throw new Exception("Keine Erlaubnis fuer das USB-Geraet");
                }
                UsbDeviceConnection verb = mgr.openDevice(treiber.getDevice());
                if (verb == null) throw new Exception("USB-Geraet laesst sich nicht oeffnen");

                port = treiber.getPorts().get(0);
                port.open(verb);
                port.setParameters(BAUD, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE);

                melde("sync", 5);
                inDenBootloader(port);
                synchronisieren(port);
                // Schadet nicht und ist bei manchen Modulen noetig, damit der
                // ROM-Loader den richtigen SPI-Flash anspricht.
                befehl(port, CMD_SPI_ATTACH, new byte[8], 0, 3000);

                schreiben(port, abbild);

                melde("check", 97);
                pruefen(port, abbild);

                // Neustart: Ende ohne Reboot-Flag, danach harter Reset ueber die Leitungen.
                befehl(port, CMD_FLASH_END, zahl32(1), 0, 3000);
                neustart(port);

                melde("done", 100);
                call.resolve(new JSObject().put("ok", true));
            } catch (Throwable e) {
                call.reject(String.valueOf(e.getMessage() == null ? e : e.getMessage()));
            } finally {
                try { if (port != null) port.close(); } catch (Throwable ignored) {}
            }
        }, "esp-flash").start();
    }

    // =====================================================================
    //  Ablauf
    // =====================================================================

    /** Laedt die Datei in den Speicher und meldet dabei 0 bis 10 Prozent. */
    private byte[] laden(String url) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(15000);
        c.setReadTimeout(60000);
        c.setInstanceFollowRedirects(true);
        if (c.getResponseCode() != 200) throw new Exception("Download: HTTP " + c.getResponseCode());
        int gesamt = c.getContentLength();
        ByteArrayOutputStream aus = new ByteArrayOutputStream(Math.max(gesamt, 1 << 20));
        try (InputStream in = c.getInputStream()) {
            byte[] puffer = new byte[16384];
            int n, summe = 0, zuletzt = -1;
            while ((n = in.read(puffer)) > 0) {
                aus.write(puffer, 0, n);
                summe += n;
                if (gesamt > 0) {
                    int pz = summe * 10 / gesamt;
                    if (pz != zuletzt) { melde("open", pz); zuletzt = pz; }
                }
            }
        }
        byte[] b = aus.toByteArray();
        if (b.length < 1024) throw new Exception("Firmware ist zu klein");
        return b;
    }

    /**
     * Klassische Reset-Folge. Die Treiberstufe auf dem Board invertiert die
     * Leitungen: RTS zieht EN, DTR zieht IO0.
     */
    private void inDenBootloader(UsbSerialPort port) throws Exception {
        port.setDTR(false); port.setRTS(true);    // EN auf Masse: Chip im Reset
        Thread.sleep(100);
        port.setDTR(true);  port.setRTS(false);   // IO0 auf Masse, EN wieder frei
        Thread.sleep(50);
        port.setDTR(false);                       // IO0 loslassen
        Thread.sleep(200);
        leeren(port);
    }

    /** Harter Neustart ins normale Programm. */
    private void neustart(UsbSerialPort port) throws Exception {
        port.setDTR(false); port.setRTS(true);
        Thread.sleep(100);
        port.setRTS(false);
    }

    /** Bis zu zehn Versuche, den ROM-Loader zu erwischen. */
    private void synchronisieren(UsbSerialPort port) throws Exception {
        byte[] nutz = new byte[36];
        nutz[0] = 0x07; nutz[1] = 0x07; nutz[2] = 0x12; nutz[3] = 0x20;
        for (int i = 4; i < 36; i++) nutz[i] = 0x55;
        Exception letzte = null;
        for (int versuch = 0; versuch < 10; versuch++) {
            try {
                befehl(port, CMD_SYNC, nutz, 0, 500);
                // Der Loader schickt auf ein SYNC mehrere Antworten. Die
                // ueberzaehligen muessen weg, sonst verrutscht alles Weitere.
                Thread.sleep(50);
                leeren(port);
                return;
            } catch (Exception e) {
                letzte = e;
                Thread.sleep(100);
            }
        }
        throw new Exception("Board antwortet nicht (" + (letzte == null ? "?" : letzte.getMessage()) + ")");
    }

    /** Schreibt das Abbild blockweise nach 0x0 und meldet 10 bis 96 Prozent. */
    private void schreiben(UsbSerialPort port, byte[] abbild) throws Exception {
        int bloecke = (abbild.length + BLOCK - 1) / BLOCK;

        byte[] anfang = new byte[16];
        setz32(anfang, 0, abbild.length);   // zu loeschende Groesse
        setz32(anfang, 4, bloecke);
        setz32(anfang, 8, BLOCK);
        setz32(anfang, 12, 0);              // Zieladresse 0x0
        // Das Loeschen eines ganzen Abbilds dauert beim ROM-Loader lange.
        befehl(port, CMD_FLASH_BEGIN, anfang, 0, 30000);

        melde("write", 10);
        for (int i = 0; i < bloecke; i++) {
            int von = i * BLOCK, laenge = Math.min(BLOCK, abbild.length - von);
            byte[] daten = new byte[BLOCK];
            System.arraycopy(abbild, von, daten, 0, laenge);
            // Der letzte Block wird mit 0xFF aufgefuellt, nicht mit Nullen -
            // geloeschter Flash ist 0xFF, so bleibt die Pruefsumme stimmig.
            for (int k = laenge; k < BLOCK; k++) daten[k] = (byte) 0xFF;

            byte[] nutz = new byte[16 + BLOCK];
            setz32(nutz, 0, BLOCK);
            setz32(nutz, 4, i);
            setz32(nutz, 8, 0);
            setz32(nutz, 12, 0);
            System.arraycopy(daten, 0, nutz, 16, BLOCK);

            befehl(port, CMD_FLASH_DATA, nutz, pruefsumme(daten), 5000);
            melde("write", 10 + (int) (86L * (i + 1) / bloecke));
        }
    }

    /** Vergleicht die MD5-Summe im Flash mit der des Abbilds. */
    private void pruefen(UsbSerialPort port, byte[] abbild) throws Exception {
        // Der Flash enthaelt volle Bloecke, die Pruefsumme muss ueber dieselbe
        // aufgefuellte Laenge laufen.
        int bloecke = (abbild.length + BLOCK - 1) / BLOCK;
        int laenge = bloecke * BLOCK;
        byte[] voll = new byte[laenge];
        java.util.Arrays.fill(voll, (byte) 0xFF);
        System.arraycopy(abbild, 0, voll, 0, abbild.length);
        String erwartet = md5(voll);

        byte[] nutz = new byte[16];
        setz32(nutz, 0, 0);
        setz32(nutz, 4, laenge);
        byte[] antwort = befehl(port, CMD_FLASH_MD5, nutz, 0, 30000);
        String bekommen = md5AusAntwort(antwort);
        if (bekommen != null && !bekommen.equalsIgnoreCase(erwartet)) {
            throw new Exception("Pruefsumme stimmt nicht");
        }
    }

    // =====================================================================
    //  Protokoll
    // =====================================================================

    /**
     * Schickt einen Befehl und wartet auf die Antwort zum selben Befehl.
     * Gibt den Nutzteil der Antwort zurueck.
     */
    private byte[] befehl(UsbSerialPort port, int cmd, byte[] nutz, int summe, int wartenMs)
            throws Exception {
        byte[] kopf = new byte[8 + nutz.length];
        kopf[0] = 0x00;
        kopf[1] = (byte) cmd;
        kopf[2] = (byte) (nutz.length & 0xFF);
        kopf[3] = (byte) ((nutz.length >> 8) & 0xFF);
        setz32(kopf, 4, summe);
        System.arraycopy(nutz, 0, kopf, 8, nutz.length);
        port.write(slip(kopf), 4000);

        long ende = System.currentTimeMillis() + wartenMs;
        while (System.currentTimeMillis() < ende) {
            byte[] p = paketLesen(port, (int) (ende - System.currentTimeMillis()));
            if (p == null) continue;
            if (p.length < 8 || (p[0] & 0xFF) != 0x01) continue;
            if ((p[1] & 0xFF) != cmd) continue;              // Antwort auf etwas anderes
            int len = (p[2] & 0xFF) | ((p[3] & 0xFF) << 8);
            byte[] rest = new byte[Math.max(0, Math.min(len, p.length - 8))];
            System.arraycopy(p, 8, rest, 0, rest.length);
            // Der ROM-Loader haengt zwei Byte an: Status und Fehlernummer.
            if (rest.length >= 2 && rest[rest.length - 2] != 0) {
                throw new Exception("Befehl " + cmd + " abgelehnt, Fehler "
                                    + (rest[rest.length - 1] & 0xFF));
            }
            return rest;
        }
        throw new Exception("Keine Antwort auf Befehl " + cmd);
    }

    /** Liest genau ein SLIP-Paket, oder null bei Zeitablauf. */
    private byte[] paketLesen(UsbSerialPort port, int wartenMs) throws Exception {
        if (wartenMs <= 0) return null;
        long ende = System.currentTimeMillis() + wartenMs;
        ByteArrayOutputStream aus = new ByteArrayOutputStream();
        boolean drin = false, entkommen = false;
        byte[] puffer = new byte[512];
        while (System.currentTimeMillis() < ende) {
            int n = port.read(puffer, 200);
            for (int i = 0; i < n; i++) {
                byte b = puffer[i];
                if (!drin) { if (b == SLIP_ENDE) drin = true; continue; }
                if (b == SLIP_ENDE) {
                    if (aus.size() == 0) continue;          // zwei Ende-Byte hintereinander
                    return aus.toByteArray();
                }
                if (entkommen) {
                    aus.write(b == (byte) 0xDC ? SLIP_ENDE : SLIP_ESC);
                    entkommen = false;
                } else if (b == SLIP_ESC) {
                    entkommen = true;
                } else {
                    aus.write(b);
                }
            }
        }
        return null;
    }

    /** Verpackt einen Rahmen in SLIP. */
    private byte[] slip(byte[] roh) {
        ByteArrayOutputStream aus = new ByteArrayOutputStream(roh.length + 16);
        aus.write(SLIP_ENDE);
        for (byte b : roh) {
            if (b == SLIP_ENDE)      { aus.write(SLIP_ESC); aus.write(0xDC); }
            else if (b == SLIP_ESC)  { aus.write(SLIP_ESC); aus.write(0xDD); }
            else                     { aus.write(b); }
        }
        aus.write(SLIP_ENDE);
        return aus.toByteArray();
    }

    private void leeren(UsbSerialPort port) {
        try {
            byte[] weg = new byte[512];
            long ende = System.currentTimeMillis() + 150;
            while (System.currentTimeMillis() < ende && port.read(weg, 50) > 0) { /* wegwerfen */ }
        } catch (Throwable ignored) {}
    }

    /** XOR ueber die Daten, mit 0xEF begonnen - so will es der Bootloader. */
    private int pruefsumme(byte[] daten) {
        int s = 0xEF;
        for (byte b : daten) s ^= (b & 0xFF);
        return s;
    }

    // =====================================================================
    //  Kleinkram
    // =====================================================================

    private void melde(String schritt, int prozent) {
        JSObject o = new JSObject();
        o.put("schritt", schritt);
        o.put("prozent", prozent);
        notifyListeners("fortschritt", o);
    }

    /** Fragt Android einmal nach der Erlaubnis und wartet auf die Antwort. */
    private boolean erlaubnisHolen(UsbManager mgr, UsbDevice geraet) {
        final CountDownLatch warten = new CountDownLatch(1);
        final boolean[] ok = {false};
        BroadcastReceiver empf = new BroadcastReceiver() {
            @Override public void onReceive(Context c, Intent i) {
                if (!ACTION_USB.equals(i.getAction())) return;
                ok[0] = i.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
                warten.countDown();
            }
        };
        try {
            IntentFilter filter = new IntentFilter(ACTION_USB);
            if (Build.VERSION.SDK_INT >= 33) {
                getContext().registerReceiver(empf, filter, Context.RECEIVER_NOT_EXPORTED);
            } else {
                getContext().registerReceiver(empf, filter);
            }
            int flaggen = Build.VERSION.SDK_INT >= 31
                ? PendingIntent.FLAG_MUTABLE | PendingIntent.FLAG_UPDATE_CURRENT
                : PendingIntent.FLAG_UPDATE_CURRENT;
            Intent absicht = new Intent(ACTION_USB).setPackage(getContext().getPackageName());
            mgr.requestPermission(geraet,
                PendingIntent.getBroadcast(getContext(), 0, absicht, flaggen));
            warten.await(60, TimeUnit.SECONDS);
        } catch (Throwable ignored) {
        } finally {
            try { getContext().unregisterReceiver(empf); } catch (Throwable ignored) {}
        }
        return ok[0] && mgr.hasPermission(geraet);
    }

    private static void setz32(byte[] z, int ab, int wert) {
        z[ab]     = (byte) (wert & 0xFF);
        z[ab + 1] = (byte) ((wert >> 8) & 0xFF);
        z[ab + 2] = (byte) ((wert >> 16) & 0xFF);
        z[ab + 3] = (byte) ((wert >> 24) & 0xFF);
    }

    private static byte[] zahl32(int wert) {
        byte[] b = new byte[4];
        setz32(b, 0, wert);
        return b;
    }

    private static String md5(byte[] daten) throws Exception {
        byte[] h = java.security.MessageDigest.getInstance("MD5").digest(daten);
        StringBuilder sb = new StringBuilder();
        for (byte b : h) sb.append(String.format("%02x", b));
        return sb.toString();
    }

    /**
     * Der ROM-Loader schickt die Summe als 32 Zeichen Text, ein Stub-Loader
     * als 16 rohe Byte. Beides wird hier akzeptiert; passt nichts davon,
     * wird nicht geprueft statt faelschlich abgebrochen.
     */
    private static String md5AusAntwort(byte[] rest) {
        if (rest == null) return null;
        int nutz = Math.max(0, rest.length - 2);     // Status und Fehlernummer abziehen
        if (nutz >= 32) {
            String s = new String(rest, 0, 32, java.nio.charset.StandardCharsets.US_ASCII);
            if (s.matches("[0-9a-fA-F]{32}")) return s;
        }
        if (nutz >= 16) {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 16; i++) sb.append(String.format("%02x", rest[i] & 0xFF));
            return sb.toString();
        }
        return null;
    }
}
