# Ocarina of Resonance

Ocarina of Resonance is a vessel-flute synthesizer for Windows VST3, web, and Android. Version 0.6.0 adds coordinated packaging for all three platforms. Play the blue-and-gold transverse ocarina with the piano roll, MIDI controllers, four sound models, 40 factory presets, and 11 stereo effects in mono legato or with up to 16 voices.

[Play the instrument](https://blinkzer0.github.io/Ocarina-of-Resonance/) · [Implementation and release plan](PLAN.md) · [Release validation](design/PHASE_7.md) · [Packaging and signing](design/PHASE_6.md) · [Phase 5 performance and MIDI](design/PHASE_5.md) · [Phase 4 effects and presets](design/PHASE_4.md) · [Phase 3 instrument interface](design/PHASE_3.md) · [Phase 2 DSP and verification](design/PHASE_2.md) · [Phase 1 architecture](design/PHASE_1.md) · [Preset schema](presets/schema-v1.json)

## Play and edit

Drag a front or thumb dial vertically to adjust its parameter. Use the mouse wheel or arrow keys for steps. Select **Fine** or hold Shift for smaller changes. Click or tap a dial to inspect its value and units, enter an exact number, or select **Reset**. A desktop double-click also resets the dial. The piano supports four octaves per range, octave buttons, and a dedicated scroll control when the window is narrow.

Choose a **Bank** and **Preset** to load a factory sound. Expand **Effects** to change individual processors, preset gain, or delay tempo. Use **Save** to keep edits in your local library and **Rename** to update a saved preset's name. Export an `.orpreset` file and open it with **Import** on any platform.

Expand **Piano roll** to compose or import a MIDI pattern. Draw on the grid, drag a note to move it, or drag its right edge to change duration. Set velocity, tempo, snap, and loop length above the grid. Use **Scroll** to pan on touchscreens and **Full screen** for either performance view. The VST3's **DAW sync** follows the host while it plays and allows local audition when idle.

## Windows VST3

Install Visual Studio with the Desktop development with C++ workload, CMake, and Git. Windows uses the Microsoft Edge WebView2 Runtime for the instrument editor. Run `build.bat`. To reuse a JUCE 8.0.17-security checkout, run `build.bat -JuceDirectory <path>`. The build embeds the generated panel assets, runs the core and host tests, and writes the VST3 to `build/windows/src/plugin/OcarinaVST_artefacts/Release/VST3/`.

Add that directory to a VST3 host's scan paths or copy the whole `.vst3` bundle to the Windows VST3 folder. Load it as an instrument and send MIDI notes. MIDI supports sustain, modulation CC1, breath CC2, expression CC11, pitch bend, and aftertouch. Use **MIDI input** to learn additional parameter mappings. The piano roll and mappings are saved with the DAW project. FL Studio is the primary Windows release target.

## Web

Use Node.js 24 and Emscripten 7.0.0. Set `EMSDK` to the installed and activated SDK or make `em++` available on `PATH`. From `web/`, run `npm ci` and `npm run build` to generate `docs/`. From the repository root, run `start.bat` and open the printed project-subpath URL. Select **Start audio**, then play with the mouse, touch, or typing keyboard. A, S, D, F, G, H, J, and K play the white keys of the middle playing octave. Choose **Enable Web MIDI** in a browser that supports controller access.

Run `npm test` from `web/` after installing Playwright Chromium with `npx playwright-core install chromium`. To use Edge or another installed Chromium browser for the smoke test, set `OCARINA_BROWSER` to its executable path. The Pages workflow publishes `docs/` once the repository's Pages source is set to GitHub Actions.

## Android

Open `android/` as an Android Studio project, or set `JAVA_HOME` to JDK 21 and `ANDROID_HOME` to the SDK. Install platform 36, NDK 29.0.14206865, and CMake 3.31.6. Run `android/build.bat`, or `sh ./gradlew assembleDebug` from `android/` on Unix. The output is `android/app/build/outputs/apk/debug/app-debug.apk`.

Select an unlocked ARM64 phone with `adb devices -l`. Install with `adb -s <device> install -r android/app/build/outputs/apk/debug/app-debug.apk`. The panel is bundled for offline use and plays through the native Oboe engine. **Audio diagnostics** in the debug APK includes an A4 test tone and short and 10-minute 16-voice profiles. Timings and xruns appear in the panel and under the `OcarinaAudio` log tag. For release-optimized DSP measurements, run `android/build.bat assembleBenchmark` and install `android/app/build/outputs/apk/benchmark/app-benchmark.apk`. This debug-signed test package has a separate app ID and retains the diagnostics.

Connect a class-compliant USB MIDI controller or select **Find Bluetooth MIDI** under **MIDI input**. Use **Audio output** to choose a latency mode and an automatic or explicit buffer in device bursts. The status line reports the active backend, sample rate, and buffer.

For a signed direct-install APK and a Play-ready AAB, configure a release keystore and run `android/build.bat assembleRelease bundleRelease`. The packaging guide covers local signing, artifact checks, and the protected GitHub Actions release. [Packaging and signing](design/PHASE_6.md)
