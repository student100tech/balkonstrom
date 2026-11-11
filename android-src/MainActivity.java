package de.elektroautonews.balkonstrom;

import android.os.Bundle;
import com.getcapacitor.BridgeActivity;

public class MainActivity extends BridgeActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        registerPlugin(UpdaterPlugin.class);
        registerPlugin(DiscoveryPlugin.class);
        super.onCreate(savedInstanceState);
    }
}
