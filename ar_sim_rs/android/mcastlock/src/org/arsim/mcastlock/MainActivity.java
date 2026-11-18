package org.arsim.mcastlock;

import android.app.Activity;
import android.content.Context;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.PowerManager;
import android.view.WindowManager;
import android.widget.TextView;

/**
 * Holds the locks the harness needs while this screen is open.
 *
 * Android's Wi-Fi stack filters multicast frames unless some app holds a MulticastLock; the
 * ar_device process runs from adb shell and cannot take one itself. The Wi-Fi lock keeps the
 * radio out of power save (low-latency mode from Android 10), and the wake lock and
 * keep-screen-on flag stop the phone from sleeping mid-run. Every lock is released when the
 * screen closes. The harness checks the log line "ar_sim locks held" to confirm.
 */
public class MainActivity extends Activity {
    private WifiManager.MulticastLock multicastLock;
    private WifiManager.WifiLock wifiLock;
    private PowerManager.WakeLock wakeLock;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        multicastLock = wifi.createMulticastLock("ar_sim");
        multicastLock.setReferenceCounted(false);
        multicastLock.acquire();

        int mode = Build.VERSION.SDK_INT >= 29
                ? WifiManager.WIFI_MODE_FULL_LOW_LATENCY
                : WifiManager.WIFI_MODE_FULL_HIGH_PERF;
        wifiLock = wifi.createWifiLock(mode, "ar_sim");
        wifiLock.setReferenceCounted(false);
        wifiLock.acquire();

        PowerManager power = (PowerManager) getSystemService(Context.POWER_SERVICE);
        wakeLock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "ar_sim:harness");
        wakeLock.setReferenceCounted(false);
        wakeLock.acquire();

        TextView text = new TextView(this);
        text.setTextSize(20);
        text.setPadding(48, 96, 48, 48);
        text.setText("ar_sim harness\n\nMulticast lock: held\nWi-Fi lock: "
                + (mode == WifiManager.WIFI_MODE_FULL_HIGH_PERF ? "high performance" : "low latency")
                + "\nWake lock: held\n\nLeave this screen open during a run.");
        setContentView(text);
        android.util.Log.i("ar_sim", "ar_sim locks held: multicast, wifi mode " + mode + ", wake");
    }

    @Override
    protected void onDestroy() {
        if (multicastLock != null && multicastLock.isHeld()) {
            multicastLock.release();
        }
        if (wifiLock != null && wifiLock.isHeld()) {
            wifiLock.release();
        }
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
        android.util.Log.i("ar_sim", "ar_sim locks released");
        super.onDestroy();
    }
}
