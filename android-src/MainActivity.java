package de.elektroautonews.balkonstrom;

import android.os.Bundle;
import android.webkit.WebSettings;
import com.getcapacitor.BridgeActivity;

public class MainActivity extends BridgeActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        registerPlugin(UpdaterPlugin.class);
        registerPlugin(DiscoveryPlugin.class);
        super.onCreate(savedInstanceState);

        // Die Android-Systemschriftgroesse vergroessert sonst auch die Weboberflaeche.
        // Dann sprengen die grossen Zahlen ihre Kreise und Kacheln. Darum fest auf 100 %.
        try {
            WebSettings s = getBridge().getWebView().getSettings();
            s.setTextZoom(100);
        } catch (Throwable ignored) {}
    }
}
