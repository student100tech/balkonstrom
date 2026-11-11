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
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.URL;
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
 * 2. falls nichts gefunden: alle Adressen im eigenen WLAN-Netz (max. /24) auf /properties/report abfragen.
 */
@CapacitorPlugin(name = "Discovery")
public class DiscoveryPlugin extends Plugin {

    private static final String SERVICE = "_zendure._tcp";

    @PluginMethod
    public void search(PluginCall call) {
        final int mdnsMs = call.getInt("mdnsMs", 4000);
        new Thread(() -> {
            try {
                int[] net = ownIpv4();
                if (net == null) { call.reject("Das Handy ist nicht mit einem WLAN verbunden."); return; }
                Map<String, JSObject> found = new LinkedHashMap<>();
                String via = "mdns";
                try { mdns(mdnsMs, found); } catch (Exception ignored) { }
                if (found.isEmpty()) { via = "scan"; scan(net, found); }
                JSArray arr = new JSArray();
                for (JSObject o : found.values()) arr.put(o);
                JSObject ret = new JSObject();
                ret.put("devices", arr);
                ret.put("via", via);
                ret.put("ownIp", ipStr(net[0]));
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
            if (c.getResponseCode() != 200) return null;
            JSONObject j = new JSONObject(read(c.getInputStream()));
            if (!j.has("properties")) return null;
            JSObject o = new JSObject();
            o.put("ip", ip);
            o.put("sn", j.optString("sn", ""));
            o.put("product", j.optString("product", ""));
            return o;
        } catch (Exception e) {
            return null;
        } finally {
            if (c != null) c.disconnect();
        }
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
