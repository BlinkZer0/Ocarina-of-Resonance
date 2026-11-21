# Phase 1 Architecture and Verification

## Shared contracts

- **DSP.** The `ocarina_core` CMake library contains the reference oscillator, gain smoothing, a velocity-sensitive envelope, and source-aware held-note tracking. JUCE, Wasm, and the Android NDK compile the same sources. The prototype validates audio plumbing independently of the Phase 2 physical model.
- **Parameters.** `src/shared/Parameters.h` owns the twelve proposed hole IDs plus Master Volume. Catalog values use the documented native units. Treat the Phase 1 ranges and defaults as calibration inputs. Preserve IDs when adjusting ranges and presentation. The reference tone consumes Master Volume through the VST3 host parameter and reports it in plugin state.
- **Events.** `src/shared/Event.h` defines normalized velocity, zero-based MIDI channel, MIDI note number, source identity, and optional block-relative frame. VST3 renders around incoming host MIDI offsets. Web and Android deliver the prototype's interactive notes at worklet or callback boundaries. The Android control producer communicates through the bounded queue and recovers from overflow by releasing all voices. A release from one source leaves other sources' held notes in place.
- **Presets.** `presets/schema-v1.json` defines the portable JSON shape. Parameter keys are stable IDs and values use native units. Each effect record carries its stable ID, bypass state, and parameter values. Phase 4 imports effects by ID and writes them in the engine's fixed signal order. Store model, requested voice mode and count, and output compensation in performance. Keep Android buffer and device selections in app preferences. `tests/fixtures/reference.orpreset` is a development fixture. [Phase 4 effects and presets](PHASE_4.md)
- **Version and identity.** `VERSION` supplies CMake, web, and Android release metadata. The plugin uses `Blz0` and `OcRs` as its JUCE manufacturer and plugin codes. Android uses `com.blinkzer0.ocarina` with a `.debug` suffix for development and an independently increasing `versionCode` for updates.

## Platform choices

- Windows uses the family's JUCE 8 and CMake build conventions. Phase 1 uses JUCE's generic parameter editor to verify host integration. Evaluate the family's WebView2 editor approach with FL Studio before building the instrument UI in Phase 3.
- Web uses TypeScript with a single-threaded Wasm instance inside an AudioWorklet. Relative asset URLs support GitHub Pages project paths. Edge and Chromium are the initial browser test baseline.
- Android uses Kotlin platform views for the prototype controls, a narrow JNI adapter, and Oboe. Android 8.0 (API 26) is the initial minimum, with ARM64 as the release ABI. The Samsung SM-S948U1 is the initial phone target. Add a representative physical ARM64 tablet to the release matrix.

## Dependency licensing

JUCE 8 offers AGPLv3 and commercial licensing options. Before distributing the VST3 prototype or release, confirm that the owner will meet JUCE's AGPLv3 terms or use an applicable commercial license. Carry the MIT-licensed VST3 SDK notices included in the pinned JUCE source. [JUCE license](https://github.com/juce-framework/JUCE/blob/8.0.17-security/LICENSE.md)

Oboe and Kotlin use Apache License 2.0. Emscripten uses MIT and University of Illinois/NCSA licensing. The web distribution includes the Emscripten, musl, and libc++ notices. Check the Android NDK runtime notices before distributing the APK. The prototype uses original code and styles with platform fonts. [Web runtime notices](../docs/licenses/) · [Oboe license](https://github.com/google/oboe/blob/1.11.0/LICENSE) · [Kotlin license](https://github.com/JetBrains/kotlin/blob/v2.2.10/license/LICENSE.txt) · [Emscripten license](https://emscripten.org/docs/introducing_emscripten/emscripten_license.html)

## Verification

| Target | Evidence |
| --- | --- |
| Shared C++ | CTest verified 440 Hz at 44.1, 48, 88.2, and 96 kHz, source-isolated release, bounded output, and queue overflow recovery. |
| Windows VST3 | The x64 bundle passed 47 Steinberg validator tests and the CTest host integration check for MIDI offsets, note release, and state recall. REAPER 7.86 loaded it as an instrument and rendered a MIDI A4 note at 48 kHz. The 24-bit stereo WAV measured 440 Hz at a peak of 0.165 FS. The FL Studio editor and host checks are pending. |
| Web | Edge and the GitHub Actions Chromium test loaded the generated build at the local `/Ocarina-of-Resonance/` subpath and measured nonzero AudioWorklet output while a note was held and silence after release. Edge repeated the audio check on the live HTTPS deployment. [GitHub Pages prototype](https://blinkzer0.github.io/Ocarina-of-Resonance/) |
| Android | The ARM64 APK passed APK signature and 16 KB ZIP-alignment checks. It installed and launched on Samsung SM-S948U1. Tapping **Play A4 test tone** on the phone logged 21,718 non-silent frames on an AAudio stream at 44.1 kHz. Tablet, OpenSL ES, and acoustic latency checks are pending. |
