# Ocarina of Resonance

Ocarina of Resonance is a vessel-flute synthesizer in development for Windows VST3, web, and Android. Version 0.4.1 staggers four main dials and one smaller subhole dial for each hand on a glazed blue-and-gold transverse ocarina. Two thumb dials sit below it. Explore four sound models, 40 factory presets, and 11 stereo effects in mono legato or with up to 16 voices.

[Play the instrument](https://blinkzer0.github.io/Ocarina-of-Resonance/) · [Implementation and release plan](PLAN.md) · [Phase 4 effects and presets](design/PHASE_4.md) · [Phase 3 instrument interface](design/PHASE_3.md) · [Phase 2 DSP and verification](design/PHASE_2.md) · [Phase 1 architecture](design/PHASE_1.md) · [Preset schema](presets/schema-v1.json)

## Play and edit

Drag a front or thumb dial vertically to adjust its parameter. Use the mouse wheel or arrow keys for steps. Select **Fine** or hold Shift for smaller changes. Click or tap a dial to inspect its value and units, enter an exact number, or select **Reset**. A desktop double-click also resets the dial. The piano supports four octaves per range, octave buttons, and a dedicated scroll control when the window is narrow.

Choose a **Bank** and **Preset** to load a factory sound. Expand **Effects** to change individual processors, preset gain, or delay tempo. Use **Save** to keep edits in your local library and **Rename** to update a saved preset's name. Export an `.orpreset` file and open it with **Import** on any platform.

## Windows VST3

Install Visual Studio with the Desktop development with C++ workload, CMake, and Git. Windows uses the Microsoft Edge WebView2 Runtime for the instrument editor. Run `build.bat`. To reuse a JUCE 8.0.17-security checkout, run `build.bat -JuceDirectory <path>`. The build embeds the generated panel assets, runs the core and host tests, and writes the VST3 to `build/windows/src/plugin/OcarinaVST_artefacts/Release/VST3/`.

Add that directory to a VST3 host's scan paths or copy the whole `.vst3` bundle to the Windows VST3 folder. Load it as an instrument and send MIDI notes. The host editor exposes the physical controls, expression settings, sound model, and voice configuration. MIDI supports sustain, breath CC2, expression CC11, pitch bend, and aftertouch. FL Studio is the primary Windows release target.

## Web

Use Node.js 24 and Emscripten 7.0.0. Set `EMSDK` to the installed and activated SDK or make `em++` available on `PATH`. From `web/`, run `npm ci` and `npm run build` to generate `docs/`. From the repository root, run `start.bat` and open the printed project-subpath URL. Select **Start audio**, then hold a note with the mouse, touch, or the Space or Enter key on a focused note button.

Run `npm test` from `web/` after installing Playwright Chromium with `npx playwright-core install chromium`. To use Edge or another installed Chromium browser for the smoke test, set `OCARINA_BROWSER` to its executable path. The Pages workflow publishes `docs/` once the repository's Pages source is set to GitHub Actions.

## Android

Open `android/` as an Android Studio project, or set `JAVA_HOME` to JDK 21 and `ANDROID_HOME` to the SDK. Install platform 36, NDK 29.0.14206865, and CMake 3.31.6. Run `android/build.bat`, or `sh ./gradlew assembleDebug` from `android/` on Unix. The output is `android/app/build/outputs/apk/debug/app-debug.apk`.

Select an unlocked ARM64 phone with `adb devices -l`. Install with `adb -s <device> install -r android/app/build/outputs/apk/debug/app-debug.apk`. The panel is bundled for offline use and plays through the native Oboe engine. The debug APK includes **Audio diagnostics** with the A4 test tone and **Profile 16 voices**. Timings and xruns appear under the `OcarinaAudio` log tag.
