# Film Simulation UFX plug-in for Android

An Android plug-in that renders a spectral film simulation on the GPU. It is
built on the film engine from [spektrafilm OFX](https://github.com/chaert-s/spektrafilm-ofx)
and the film model and profiles from
[spektrafilm](https://github.com/andreavolpato/spektrafilm) by Andrea Volpato.

The plug-in ships as its own APK. It has no screen or launcher icon: apps that
support UFX plug-ins find it once it is installed and run it on their own Vulkan
device.

## What it does

- Simulates film exposure, development, printing and scanning from spectral
  film and paper profiles (20 film stocks, 8 papers).
- Adds grain, halation, diffusion filters, DIR couplers and scanner effects.
- Renders photos with the full spectral pipeline and video with precomputed
  colour tables. GPUs that cannot filter 32-bit float textures blend the
  colour tables in the shader at full precision.
- Describes its own settings (pages, sliders, toggles, choice lists) so the host
  app can build the controls.

## Building

Requirements:

- Android SDK with NDK 28.2.13676358 and CMake 3.22.1
- JDK 17
- Python 3 with `numpy`, `scipy` and `colour-science` (the build generates the
  profile tables from the JSON profiles)

If Python is not on your `PATH`, point the build at it in `local.properties`:

```
python.executable=C:/path/to/python.exe
```

Then build:

```
./gradlew assembleDebug
```

The APK is written to `plugin/build/outputs/apk/debug/plugin-debug.apk`.
Release builds are unsigned; sign them with your own key.

## How hosts load it

The interface is described in
[`plugin/src/main/cpp/ufx/include/ufx/ufx_plugin.h`](plugin/src/main/cpp/ufx/include/ufx/ufx_plugin.h)
(MIT licensed). In short:

1. The plug-in's manifest declares a service that answers the `ufx.PLUGIN`
   action and names an entry class in `ufx.ENTRY_CLASS` meta-data.
2. The host finds it with `PackageManager.queryIntentServices`, opens it with
   `createPackageContext(CONTEXT_INCLUDE_CODE | CONTEXT_IGNORE_SECURITY)` and
   calls the entry class's static `ufxEntryAddress()`, which loads the native
   library and returns the address of `ufx_get_plugin`.
3. Through that function the host reads the plug-in's settings, creates an
   instance on its Vulkan device and renders `RGBA16F` images.

## Project layout

```
plugin/src/main/cpp/
  spektrafilm/   film engine: renderer, shaders, profiles and build tools
  plugin/        UFX wrapper: settings table and plug-in entry points
  ufx/           UFX plug-in interface (MIT)
  cmake/         shader embedding helper
plugin/src/main/java/spektrafilm/ufx/
  UfxEntry.java          loads the native library for the host
  UfxPluginService.java  marks the APK as a UFX plug-in
```

## Licenses and credits

- Source code: GNU General Public License v3.0 or later. See [LICENSE](LICENSE).
- Film engine: spektrafilm OFX, Copyright (C) 2026 Aedan Diez.
- Film profiles and spectral data: spektrafilm by Andrea Volpato, licensed
  CC BY-SA 4.0. See [SPEKTRAFILM_LICENSE.txt](SPEKTRAFILM_LICENSE.txt).
- UFX interface header: MIT.

See [NOTICE](NOTICE) for the changes made to the film engine.
