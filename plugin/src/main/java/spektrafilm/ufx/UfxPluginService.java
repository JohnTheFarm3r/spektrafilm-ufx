// SPDX-License-Identifier: GPL-3.0-or-later
package spektrafilm.ufx;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;

/** Marks this APK as a UFX plug-in. Hosts find it but never start or bind it. */
public final class UfxPluginService extends Service {
    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
