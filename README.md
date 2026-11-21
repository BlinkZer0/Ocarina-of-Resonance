# Ocarina of Resonance

Ocarina of Resonance is a vessel-flute synthesizer in development for Windows VST3, web, and Android. The Phase 2 prototype shares a modeled air jet and cavity resonator across all three platforms. Choose Ceramic Authentic, Retro Fantasy, Crystal Resonator, or Electronic Hybrid and play in mono legato or with up to 16 voices.

[Play the web prototype](https://blinkzer0.github.io/Ocarina-of-Resonance/) · [Implementation and release plan](PLAN.md) · [Phase 2 DSP and verification](design/PHASE_2.md) · [Phase 1 architecture](design/PHASE_1.md) · [Preset schema](presets/schema-v1.json)

## Windows VST3

Install Visual Studio with the Desktop development with C++ workload, CMake, and Git. Run `build.bat`. To reuse a JUCE 8.0.17-security checkout, run `build.bat -JuceDirectory <path>`. The build runs the core and host tests and writes the VST3 to `build/windows/src/plugin/OcarinaVST_artefacts/Release/VST3/`.

Add that directory to a VST3 host's scan paths or copy the whole `.vst3` bundle to the Windows VST3 folder. Load it as an instrument and send MIDI notes. The host editor exposes the physical controls, expression settings, sound model, and voice configuration. MIDI supports sustain, breath CC2, expression CC11, pitch bend, and aftertouch. FL Studio is the primary Windows release target.

## Web

Use Node.js 24 and Emscripten 7.0.0. Set `EMSDK` to the installed and activated SDK or make `em++` available on `PATH`. From `web/`, run `npm ci` and `npm run build` to generate `docs/`. From the repository root, run `start.bat` and open the printed project-subpath URL. Select **Start audio**, then hold a note with the mouse, touch, or the Space or Enter key on a focused note button.

Run `npm test` from `web/` after installing Playwright Chromium with `npx playwright-core install chromium`. To use Edge or another installed Chromium browser for the smoke test, set `OCARINA_BROWSER` to its executable path. The Pages workflow publishes `docs/` once the repository's Pages source is set to GitHub Actions.

## Android

Open `android/` as an Android Studio project, or set `JAVA_HOME` to JDK 21 and `ANDROID_HOME` to the SDK. Install platform 36, NDK 29.0.14206865, and CMake 3.31.6. Run `android/build.bat`, or `sh ./gradlew assembleDebug` from `android/` on Unix. The output is `android/app/build/outputs/apk/debug/app-debug.apk`.

Select an unlocked ARM64 phone with `adb devices -l`. Install with `adb -s <device> install -r android/app/build/outputs/apk/debug/app-debug.apk`. Open Ocarina of Resonance, choose a sound model and polyphony, and hold a note or select **Play A4 test tone**. The app displays the active Oboe backend and stream rate. The debug APK's **Profile 16 voices** button logs callback timing and xruns under the `OcarinaAudio` tag.
