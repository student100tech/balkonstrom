package de.elektroautonews.balkonstrom;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.nsd.NsdManager;
import android.net.nsd.NsdServiceInfo;
import android.net.wifi.WifiManager;
import com.getcapacitor.JSArray;
import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.HttpURLConnection;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.NetworkInterface;
import java.net.Socket;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import org.json.JSONObject;

/**
 * Sucht Zendure-Geräte im WLAN:
 * 1. per mDNS (Dienst "_zendure._tcp", so meldet sich das Gerät laut zenSDK-Doku),
 * 2. danach IMMER alle Adressen im eigenen WLAN-Netz (max. /24) auf /properties/report abfragen.
 *
 * Der zweite Schritt lief frueher nur, wenn mDNS nichts fand. Dadurch blieb ein zweites
 * Zendure-Geraet (z. B. das Smart Meter 3CT) unsichtbar, sobald der Speicher sich per
 * mDNS gemeldet hatte. Jetzt werden beide Wege zusammengefuehrt.
 */
@CapacitorPlugin(name = "Discovery")
public class DiscoveryPlugin extends Plugin {

    private static final String SERVICE = "_zendure._tcp";

    @PluginMethod
    public void search(PluginCall call) {
        final int mdnsMs = call.getInt("mdnsMs", 4000);
        final int passes = Math.max(1, Math.min(3, call.getInt("passes", 1)));
        new Thread(() -> {
            try {
                int[] net = ownIpv4();
                if (net == null) { call.reject("Das Handy ist nicht mit einem WLAN verbunden."); return; }
                Map<String, JSObject> found = new LinkedHashMap<>();
                try { mdns(mdnsMs, found); } catch (Exception ignored) { }
                int perMdns = found.size();
                // Immer absuchen, damit stille Geraete nicht fehlen. Mehrere Durchlaeufe,
                // weil ein beschaeftigtes Geraet eine einzelne Anfrage verschlucken kann.
                for (int i = 0; i < passes; i++) scan(net, found);
                String via = perMdns > 0 ? (found.size() > perMdns ? "mdns+scan" : "mdns") : "scan";
                JSArray arr = new JSArray();
                for (JSObject o : found.values()) arr.put(o);
                JSObject ret = new JSObject();
                ret.put("devices", arr);
                ret.put("via", via);
                ret.put("ownIp", ipStr(net[0]));
                // Fuer den Netzbericht: welches Netz wurde abgesucht und wie breit.
                int prefix = Math.max(net[1], 24);
                ret.put("prefix", prefix);
                ret.put("netz", ipStr(net[0] & (-1 << (32 - prefix))));
                ret.put("geprueft", prefix >= 31 ? 0 : (1 << (32 - prefix)) - 2);
                // Getrennt gehalten: ein Dongle, der nur per UDP antwortet, soll nicht
                // versehentlich als Speicher ausgewaehlt werden koennen.
                JSArray ua = new JSArray();
                for (JSObject o : wifikit(net, call.getInt("udpMs", 1500))) ua.put(o);
                ret.put("udp", ua);
                call.resolve(ret);
            } catch (Exception e) {
                call.reject(e.getMessage() != null ? e.getMessage() : e.toString());
            }
        }).start();
    }

    // ---------- mDNS ----------
    private void mdns(int ms, Map<String, JSObject> found) throws InterruptedException {
        Context ctx = getContext();
        WifiManager wm = (WifiManager) ctx.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        WifiManager.MulticastLock lock = null;
        if (wm != null) { lock = wm.createMulticastLock("balkonstrom"); lock.setReferenceCounted(false); lock.acquire(); }
        NsdManager nsd = (NsdManager) ctx.getSystemService(Context.NSD_SERVICE);
        final LinkedBlockingQueue<NsdServiceInfo> queue = new LinkedBlockingQueue<>();
        NsdManager.DiscoveryListener dl = new NsdManager.DiscoveryListener() {
            @Override public void onDiscoveryStarted(String t) { }
            @Override public void onDiscoveryStopped(String t) { }
            @Override public void onStartDiscoveryFailed(String t, int c) { }
            @Override public void onStopDiscoveryFailed(String t, int c) { }
            @Override public void onServiceFound(NsdServiceInfo s) { queue.offer(s); }
            @Override public void onServiceLost(NsdServiceInfo s) { }
        };
        try {
            nsd.discoverServices(SERVICE, NsdManager.PROTOCOL_DNS_SD, dl);
            long end = System.currentTimeMillis() + ms;
            while (true) {
                long left = end - System.currentTimeMillis();
                if (left <= 0) break;
                NsdServiceInfo s = queue.poll(left, TimeUnit.MILLISECONDS);
                if (s == null) break;
                NsdServiceInfo r = resolve(nsd, s, 3000);
                if (r == null || r.getHost() == null) continue;
                InetAddress host = r.getHost();
                if (!(host instanceof Inet4Address)) continue;
                String ip = host.getHostAddress();
                JSObject o = probe(ip);
                if (o == null) { o = new JSObject(); o.put("ip", ip); }
                o.put("name", s.getServiceName());
                found.put(ip, o);
                // Nach dem ersten Treffer nur noch kurz auf weitere Geräte warten
                end = Math.min(end, System.currentTimeMillis() + 1500);
            }
        } finally {
            try { nsd.stopServiceDiscovery(dl); } catch (Exception ignored) { }
            if (lock != null) lock.release();
        }
    }

    private NsdServiceInfo resolve(NsdManager nsd, NsdServiceInfo s, int ms) throws InterruptedException {
        final CountDownLatch latch = new CountDownLatch(1);
        final NsdServiceInfo[] out = new NsdServiceInfo[1];
        try {
            nsd.resolveService(s, new NsdManager.ResolveListener() {
                @Override public void onResolveFailed(NsdServiceInfo i, int c) { latch.countDown(); }
                @Override public void onServiceResolved(NsdServiceInfo i) { out[0] = i; latch.countDown(); }
            });
        } catch (Exception e) { return null; }
        latch.await(ms, TimeUnit.MILLISECONDS);
        return out[0];
    }

    // ---------- Netz absuchen ----------
    private void scan(int[] net, Map<String, JSObject> found) throws InterruptedException {
        int prefix = Math.max(net[1], 24);               // höchstens 254 Adressen
        int mask = -1 << (32 - prefix);
        int base = net[0] & mask;
        int count = (1 << (32 - prefix)) - 2;
        ExecutorService ex = Executors.newFixedThreadPool(48);
        List<Future<JSObject>> fs = new ArrayList<>();
        for (int i = 1; i <= count; i++) {
            final int a = base + i;
            if (a == net[0]) continue;
            final String ip = ipStr(a);
            fs.add(ex.submit(() -> probe(ip)));
        }
        ex.shutdown();
        for (Future<JSObject> f : fs) {
            try { JSObject o = f.get(); if (o != null) found.put(o.getString("ip"), o); } catch (Exception ignored) { }
        }
    }

    /** Fragt http://ip/properties/report ab – antwortet dort ein Zendure-Gerät, kommen IP, Seriennummer und Modell zurück. */
    private JSObject probe(String ip) {
        HttpURLConnection c = null;
        try {
            c = (HttpURLConnection) new URL("http://" + ip + "/properties/report").openConnection();
            c.setConnectTimeout(700);
            c.setReadTimeout(2500);
            if (c.getResponseCode() != 200) throw new Exception("kein 200");
            JSONObject j = new JSONObject(read(c.getInputStream()));
            // Das Smart Meter 3CT antwortet auf denselben Pfad, liefert aber eine ganz
            // andere Struktur: total_power und die drei Phasen, ohne properties, sn oder
            // product. Die alte Pruefung hat es deshalb weggeworfen. Auf /properties/report
            // antwortet ohnehin nichts anderes als ein Zendure-Geraet, also reicht gueltiges JSON.
            if (j.length() == 0) throw new Exception("leeres JSON");
            JSObject o = new JSObject();
            o.put("ip", ip);
            o.put("sn", j.optString("sn", ""));
            o.put("product", j.optString("product", ""));
            // Hinweis fuer die App, falls das Geraet sich nicht selbst benennt
            o.put("meter", j.has("total_power") || j.has("totalPower"));
            return o;
        } catch (Exception e) {
            // Kein Zendure-Geraet. Vielleicht spricht dort aber Modbus TCP – so meldet
            // sich z. B. ein GoodWe-Speicher am LAN-Dongle. Port 502 ist eindeutig
            // genug, im Heimnetz lauscht dort sonst praktisch nichts.
            int unit = modbusUnit(ip);
            if (unit < 0) return null;
            JSObject o = new JSObject();
            o.put("ip", ip);
            o.put("sn", "");
            o.put("product", "");
            o.put("meter", false);
            o.put("modbus", true);
            o.put("unit", unit);
            return o;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    // ---------- Modbus TCP ----------

    /** Probiert die ueblichen Geraeteadressen durch. Rueckgabe: die erste, die antwortet, sonst -1. */
    private int modbusUnit(String ip) {
        for (int unit : new int[] { 1, 247 }) {
            int[] r = modbusRead(ip, unit, 35100, 1, 350, 1200);
            if (r != null) return unit;                 // Antwort kam an, auch eine Fehlerantwort zaehlt
        }
        return -1;
    }

    /**
     * Liest Halteregister (Funktionscode 3). Rueckgabe: die Werte, bei einer
     * Modbus-Fehlerantwort ein leeres Feld, bei gar keiner Antwort null.
     */
    private int[] modbusRead(String ip, int unit, int start, int count, int connectMs, int readMs) {
        Socket so = null;
        try {
            so = new Socket();
            so.connect(new InetSocketAddress(ip, 502), connectMs);
            so.setSoTimeout(readMs);
            byte[] req = new byte[] {
                0x00, 0x01,                                   // Vorgangsnummer
                0x00, 0x00,                                   // Protokoll 0 = Modbus
                0x00, 0x06,                                   // Laenge der folgenden Bytes
                (byte) unit, 0x03,                            // Geraeteadresse, Funktionscode 3
                (byte) (start >> 8), (byte) start,
                (byte) (count >> 8), (byte) count
            };
            OutputStream out = so.getOutputStream();
            out.write(req); out.flush();
            DataInputStream in = new DataInputStream(so.getInputStream());
            byte[] head = new byte[8];
            in.readFully(head);
            if (head[2] != 0 || head[3] != 0) return null;    // kein Modbus
            int fc = head[7] & 0xFF;
            if (fc == 0x83) { in.read(); return new int[0]; } // Fehlerantwort – Geraet spricht Modbus
            if (fc != 0x03) return null;
            int bytes = in.read();
            if (bytes < 0 || bytes > 250) return null;
            byte[] data = new byte[bytes];
            in.readFully(data);
            int[] regs = new int[bytes / 2];
            for (int i = 0; i < regs.length; i++)
                regs[i] = ((data[2 * i] & 0xFF) << 8) | (data[2 * i + 1] & 0xFF);
            return regs;
        } catch (Exception e) {
            return null;
        } finally {
            try { if (so != null) so.close(); } catch (Exception ignored) { }
        }
    }

    /**
     * Liest mehrere Registerbloecke roh aus – fuer den Diagnosebericht. Welche Register
     * ein GoodWe-Geraet fuehrt, wissen wir noch nicht; deshalb werden die ueblichen
     * Bereiche abgefragt und unausgewertet zurueckgegeben.
     */
    @PluginMethod
    public void modbus(PluginCall call) {
        final String ip = call.getString("ip", "");
        new Thread(() -> {
            try {
                if (ip.isEmpty()) { call.reject("keine IP"); return; }
                int unit = call.getInt("unit", 0) > 0 ? call.getInt("unit", 1) : modbusUnit(ip);
                JSObject ret = new JSObject();
                ret.put("ip", ip);
                ret.put("unit", unit);
                if (unit < 0) { ret.put("blocks", new JSArray()); call.resolve(ret); return; }
                int[][] bereiche = {
                    { 35000, 32 },    // Geraetetyp, Seriennummer, oft als Text
                    { 35100, 125 },   // Laufzeitwerte 1
                    { 35225, 125 },   // Laufzeitwerte 2
                    { 36000, 60 },    // Akku
                    { 37000, 30 }     // Zaehler
                };
                JSArray arr = new JSArray();
                for (int[] b : bereiche) {
                    JSObject o = new JSObject();
                    o.put("start", b[0]);
                    o.put("count", b[1]);
                    int[] r = modbusRead(ip, unit, b[0], b[1], 1200, 2500);
                    if (r == null) o.put("error", "keine Antwort");
                    else if (r.length == 0) o.put("error", "Modbus-Fehlerantwort");
                    else {
                        JSArray v = new JSArray();
                        for (int x : r) v.put(x);
                        o.put("regs", v);
                    }
                    arr.put(o);
                }
                ret.put("blocks", arr);
                call.resolve(ret);
            } catch (Exception e) {
                call.reject(e.getMessage() == null ? e.toString() : e.getMessage());
            }
        }).start();
    }

    private static String read(InputStream in) throws java.io.IOException {
        try (InputStream is = in) {
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = is.read(buf)) > 0) {
                bo.write(buf, 0, n);
                if (bo.size() > 300000) break;
            }
            return bo.toString("UTF-8");
        }
    }

    // ---------- GoodWe-Dongle per Rundruf ----------

    /**
     * Fragt GoodWe-WLAN-Module so ab, wie es die Bibliothek "goodwe" tut: der Text
     * WIFIKIT-214028-READ als UDP-Rundruf auf Port 48899. Ein Modul antwortet mit
     * seiner Adresse. Steht in der Antwort dtls_port:8899, laeuft die neuere
     * verschluesselte Firmware, bei der einfaches UDP auf 8899 nicht mehr geht.
     *
     * Der Rundruf hilft genau dann, wenn Port 502 zu ist: so sehen wir, ob ueberhaupt
     * ein Dongle im Netz haengt, statt nur "nichts gefunden" melden zu koennen.
     */
    private List<JSObject> wifikit(int[] net, int wartenMs) {
        Map<String, JSObject> aus = new LinkedHashMap<>();
        DatagramSocket so = null;
        try {
            so = new DatagramSocket();
            so.setBroadcast(true);
            so.setSoTimeout(250);
            byte[] frage = "WIFIKIT-214028-READ".getBytes(StandardCharsets.US_ASCII);
            for (String ziel : new String[] { rundrufAdresse(net), "255.255.255.255" }) {
                if (ziel == null) continue;
                try {
                    so.send(new DatagramPacket(frage, frage.length, InetAddress.getByName(ziel), 48899));
                } catch (Exception ignored) { }
            }
            long ende = System.currentTimeMillis() + Math.max(300, wartenMs);
            byte[] puffer = new byte[1024];
            while (System.currentTimeMillis() < ende) {
                try {
                    DatagramPacket p = new DatagramPacket(puffer, puffer.length);
                    so.receive(p);
                    String text = new String(p.getData(), 0, p.getLength(), StandardCharsets.US_ASCII).trim();
                    if (text.isEmpty()) continue;
                    String ip = p.getAddress().getHostAddress();
                    if (ip.equals(ipStr(net[0]))) continue;          // eigenes Echo
                    JSObject o = new JSObject();
                    o.put("ip", ip);
                    o.put("antwort", text);
                    o.put("dtls", text.toLowerCase().contains("dtls"));
                    aus.put(ip, o);
                } catch (java.net.SocketTimeoutException weiter) {
                    // noch nichts da – bis zum Ende der Wartezeit erneut lauschen
                } catch (Exception e) {
                    break;
                }
            }
        } catch (Exception ignored) {
        } finally {
            if (so != null) so.close();
        }
        return new ArrayList<>(aus.values());
    }

    /** Rundruf-Adresse des eigenen Netzes, bei einem /24 also z. B. 192.168.178.255. */
    private static String rundrufAdresse(int[] net) {
        int prefix = Math.max(net[1], 8);
        if (prefix >= 31) return null;
        int mask = -1 << (32 - prefix);
        return ipStr((net[0] & mask) | ~mask);
    }

    // ---------- eigene WLAN-Adresse ----------
    private int[] ownIpv4() {
        try {
            ConnectivityManager cm = (ConnectivityManager) getContext().getSystemService(Context.CONNECTIVITY_SERVICE);
            for (Network n : cm.getAllNetworks()) {
                NetworkCapabilities nc = cm.getNetworkCapabilities(n);
                if (nc == null || !nc.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) continue;
                LinkProperties lp = cm.getLinkProperties(n);
                if (lp == null) continue;
                for (LinkAddress la : lp.getLinkAddresses()) {
                    InetAddress a = la.getAddress();
                    if (a instanceof Inet4Address && !a.isLoopbackAddress()) return new int[]{toInt(a), la.getPrefixLength()};
                }
            }
        } catch (Exception ignored) { }
        try {   // Rückfall: Netzwerkschnittstelle "wlan…"
            for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!ni.isUp() || !ni.getName().startsWith("wlan")) continue;
                for (java.net.InterfaceAddress ia : ni.getInterfaceAddresses()) {
                    if (ia.getAddress() instanceof Inet4Address) return new int[]{toInt(ia.getAddress()), ia.getNetworkPrefixLength()};
                }
            }
        } catch (Exception ignored) { }
        return null;
    }

    private static int toInt(InetAddress a) {
        byte[] b = a.getAddress();
        return ((b[0] & 255) << 24) | ((b[1] & 255) << 16) | ((b[2] & 255) << 8) | (b[3] & 255);
    }

    private static String ipStr(int a) {
        return ((a >>> 24) & 255) + "." + ((a >>> 16) & 255) + "." + ((a >>> 8) & 255) + "." + (a & 255);
    }
}
