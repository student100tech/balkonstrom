package de.elektroautonews.balkonstrom;

import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import androidx.core.content.FileProvider;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/** Lädt eine neue APK von GitHub herunter und öffnet den Android-Installationsdialog. */
@CapacitorPlugin(name = "Updater")
public class UpdaterPlugin extends Plugin {

    private static final String ALLOWED = "https://github.com/student100tech/balkonstrom/releases/download/";

    @PluginMethod
    public void downloadAndInstall(PluginCall call) {
        final String url = call.getString("url");
        if (url == null || !url.startsWith(ALLOWED) || !url.endsWith(".apk")) {
            call.reject("Ungültige Download-Adresse");
            return;
        }
        new Thread(() -> {
            try {
                if (Build.VERSION.SDK_INT >= 26 && !getContext().getPackageManager().canRequestPackageInstalls()) {
                    Intent s = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                            Uri.parse("package:" + getContext().getPackageName()));
                    s.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    getContext().startActivity(s);
                    call.reject("Bitte einmalig „Aus dieser Quelle zulassen“ einschalten und dann erneut auf „Jetzt updaten“ tippen.");
                    return;
                }
                File out = new File(getContext().getCacheDir(), "update.apk");
                HttpURLConnection c = open(url);
                int code = c.getResponseCode();
                int hops = 0;
                while ((code == 301 || code == 302 || code == 303 || code == 307 || code == 308) && hops++ < 5) {
                    String loc = c.getHeaderField("Location");
                    c.disconnect();
                    c = open(loc);
                    code = c.getResponseCode();
                }
                if (code != 200) throw new IOException("Download fehlgeschlagen (HTTP " + code + ")");
                try (InputStream in = c.getInputStream(); OutputStream os = new FileOutputStream(out)) {
                    byte[] buf = new byte[65536];
                    int n;
                    while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
                }
                c.disconnect();

                Uri uri = FileProvider.getUriForFile(getContext(), getContext().getPackageName() + ".fileprovider", out);
                Intent i = new Intent(Intent.ACTION_VIEW);
                i.setDataAndType(uri, "application/vnd.android.package-archive");
                i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_ACTIVITY_NEW_TASK);
                getContext().startActivity(i);
                call.resolve();
            } catch (Exception e) {
                call.reject(e.getMessage() != null ? e.getMessage() : e.toString());
            }
        }).start();
    }

    private static HttpURLConnection open(String u) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(u).openConnection();
        c.setInstanceFollowRedirects(true);
        c.setConnectTimeout(15000);
        c.setReadTimeout(60000);
        return c;
    }
}
