// SPDX-License-Identifier: GPL-3.0-or-later
package spektrafilm.ufx;

/** Loaded by UFX hosts through the plug-in's own class loader. */
public final class UfxEntry {
    private UfxEntry() {
    }

    /** Loads the native plug-in and returns the address of ufx_get_plugin. */
    public static long ufxEntryAddress() {
        System.loadLibrary("filmsim_ufx");
        return nativeEntryAddress();
    }

    private static native long nativeEntryAddress();
}
