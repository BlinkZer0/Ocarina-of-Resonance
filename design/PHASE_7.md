# Phase 7 Release Validation

The 0.6.0 Windows VST3 loads and plays in FL Studio and REAPER. The live GitHub Pages build plays through its bundled Wasm AudioWorklet. All 40 factory presets rendered on Windows, Wasm, and a physical ARM64 Android phone. Production Android signing and parts of the physical device matrix remain open.

## Acceptance results

The DAW and deployed web checks used the packages from commit `38eb624`. The Phase 7 source rebuilt all three targets, passed the 3 Windows and 9 web automated tests, and repeated the Steinberg validation. The Android diagnostics use the same 0.6.0 DSP and factory catalog. Android version code is 7.

| Plan check | Result | Coverage and remaining work |
| --- | --- | --- |
| Windows VST3 | Passed | Steinberg validator 3.8.1 build 84 passed 47 checks. FL Studio 26.1.7.5654 scanned the bundle from the system VST3 folder and played a note. Its Windows audio session measured zero before and after the note and a linear peak of 0.13006 during it. REAPER 7.86 loaded the extracted bundle, produced audio, exchanged Breath Pressure automation in both directions, and followed host play, seek, and stop. The automated VST3 host test covers project state recall. |
| Live browser | Passed in Chromium | The deployed HTTPS project subpath started audio, imported MIDI, rendered and stopped the roll, and explained missing audio support in a separate browser context. Browser tests cover typing, touch layouts, preset operations, and the sequencer. |
| Android install and sound | QA passed, production pending | On a physical Samsung SM-S948U1, the QA-signed standalone APK launched and the bundletool universal APK updated it with the same QA certificate. The debug app rendered through AAudio. The APK bundles the panel and requests no network permission. Production signing, manual airplane-mode playback, and a physical tablet remain open. |
| Factory presets | Numerical checks passed | All three renderers accepted the same 40 ordered IDs. Gain, balance, finite output, and peak measurements are recorded below. Representative renders are available for listening. Listening sign-off remains open. |
| Finger holes | Automated and partial physical coverage | The shared browser panel checks all 12 controls, fine adjustments, reset, and phone and tablet hit zones. REAPER round-tripped native Breath Pressure automation. The Samsung debug app passed native breath shaping. Physical two-finger dial interaction and accidental-scroll checks remain open. |
| Android keyboard and piano roll | Passed for exercised paths | Physical Samsung playback, stop, rewind, both full-screen views, and rotation recall were exercised by operating the bundled panel. Rotation restored the pattern, stopped transport, and cleared held voices. Physical simultaneous touch and sustain sign-off remain open. |
| Real-time audio | Bluetooth callback target met | The release-optimized Samsung build ran 16 voices for 10 minutes on AAudio over Bluetooth A2DP. Speaker and wired soaks, a physical tablet, shared-mode retry, OpenSL ES, route changes, interruptions, and note-to-sound latency remain open. |
| Android MIDI controllers | Physical hardware pending | Android's virtual MIDI port covered fragmented messages, running status, sustain, and disconnect. USB host and BLE MIDI controller tests, including unplug or disconnect and reconnect, still require physical controllers. |
| Portable presets | Passed in all six directions | Windows and Android system pickers and browser downloads passed. Each destination played the imported patch and matched the synthesis, effects, and performance snapshot. Automated tests cover legacy schema migration and invalid input. |

For Windows, copy the complete extracted `.vst3` bundle to `C:\Program Files\Common Files\VST3`. Open **Options → Manage plugins → Find installed plugins** in FL Studio, then add Ocarina of Resonance as a generator. Play a note with the typing keyboard or the piano roll. [Image-Line plugin installation](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/basics_externalplugins.htm) · [Steinberg VST3 validator](https://steinbergmedia.github.io/vst3_dev_portal/pages/What%2Bis%2Bthe%2BVST%2B3%2BSDK/Validator.html)

For the browser check, open the live site in a fresh Chromium context, select **Start audio**, and play the keyboard and a short imported MIDI pattern. Confirm the meter responds and held notes clear on stop. For the remaining physical Android checks, follow the signed install and AAB steps in the packaging guide. [Live instrument](https://blinkzer0.github.io/Ocarina-of-Resonance/) · [Packaging and signing](PHASE_6.md)

## Factory render comparison

Each build rendered the same two-second test at 48 kHz. C4 at velocity 0.8 played for the first second. The stereo RMS window was 0.5 to 1 second. The second second captured the release and effects tails. Peak is measured across both channels for the entire render.

| Renderer | Preset count | RMS range, dBFS | Highest sample peak |
| --- | ---: | ---: | ---: |
| Windows x64 | 40 | −21.00481 to −20.99508 | 0.27874 |
| WebAssembly | 40 | −21.00481 to −20.99508 | 0.27874 |
| Samsung ARM64 | 40 | −21.00481 to −20.99507 | 0.27874 |

The largest per-preset difference across platforms was 0.000020 dB RMS and 0.000010 linear peak. The physical Samsung core test measured a worst tuning error of 2.39531 cents across its 24 note and sample-rate combinations. The Windows offline benchmark rendered 10 seconds of 16-voice Electronic Hybrid audio in 79.337 ms. The offline benchmark measures synthesis without effects or audio-device timing.

Build with `build.bat` to run the Windows unit tests. Use `build/windows/Release/ocarina_factory_test.exe presets/factory-vectors.tsv` for the per-preset table, `build/windows/Release/ocarina_core_test.exe --benchmark` for the offline benchmark, and `build/windows/Release/ocarina_core_test.exe --render <existing-directory>` for the four model examples. For ARM64, configure the top-level CMake project with the Android NDK toolchain, `ANDROID_ABI=arm64-v8a`, `ANDROID_PLATFORM=26`, `ANDROID_STL=c++_static`, `CMAKE_BUILD_TYPE=Release`, `OCARINA_BUILD_VST3=OFF`, and `BUILD_TESTING=ON`. Run the two native test executables and the factory vectors on the phone using `adb`.

Save both factory tables as UTF-8 and run `node scripts/compare-factory-wasm.mjs windows.tsv android.tsv` from the repository root. The script prints the Wasm rows as TSV and writes the comparison summary to standard error. Each row contains preset ID, RMS in dBFS, peak amplitude, and summed stereo difference.

## Android audio measurements

Use `android/build.bat assembleBenchmark` to build the ARM64 diagnostic package with release-optimized C++ (`-O3 -DNDEBUG`). Install the `benchmark` APK, choose **Balanced** and **Automatic** under **Audio output**, then load **Mystic Cathedral**. Set Master to 2% to reproduce the quiet test level. Open **Audio diagnostics** and select **Profile 16 voices for 10 min**. Keep the app foreground on the selected audio route. The profile keeps the screen awake and releases its sources when it ends. Read the counts in the panel or with `adb logcat -d -s OcarinaAudio:I`. This package uses a debug signature and has a separate app ID.

The reference Samsung SM-S948U1 reported Android 17 and negotiated AAudio at 44,100 Hz with a 2,646-frame buffer and 882 frames per burst. The played chord covered MIDI notes 48–63 and used Mystic Cathedral's delay and reverb. The test targets a 99th-percentile callback time below 70% of its frame deadline and zero steady-state underruns over 600 seconds.

| Build | Output route | Duration | Callbacks at 16 voices | Callback p99 | Callback peak | Underruns |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Debug | Default, route unrecorded | 600 s | 55,759 | 74% | 343% | 0 |
| Release-optimized benchmark | Bluetooth A2DP | 600 s | 59,731 | 17% | 192% | 0 |

The optimized run met the numerical target on the observed route. Its peak exceeded one callback deadline, and the stream reported zero underruns. Audible glitch sign-off and note-to-sound timing still require physical monitoring.

## Portable presets

The [preset schema](../presets/schema-v1.json) supplies synthesis, effects, and performance values to each target. Three distinct fixtures were created by saving a different factory-based preset on each platform. Each target loaded the other two and produced audio. The native libraries and browser re-exports preserved identical synthesis, effects, and performance objects.

| Source | Destination | Verified path |
| --- | --- | --- |
| Browser | Windows | Browser download, Windows system file picker, native library and audio |
| Browser | Android | Browser download, Android system document picker, native library and audio |
| Windows | Browser | Windows system export, browser file input, re-export and audio |
| Windows | Android | Windows system export, Android system document picker, native library and audio |
| Android | Browser | Android system export, browser file input, re-export and audio |
| Android | Windows | Android system export, Windows system file picker, native library and audio |

The Windows and Android system exports matched their native library snapshots. Use **Save** and **Export** on the source and **Import** on the destination to reproduce a transfer. Play a note and compare the saved values.

## Production release

The packaging rehearsal built all three targets. The signed release requires a production Android key in the protected `release-signing` environment and verification on a physical device. The remaining physical MIDI, tablet, audio-route, and listening checks above are needed for full completion of the plan. [GitHub packaging rehearsal](https://github.com/BlinkZer0/Ocarina-of-Resonance/actions/runs/44915105172) · [Signing configuration](PHASE_6.md#github-release)
