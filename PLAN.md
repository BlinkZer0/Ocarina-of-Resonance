# Ocarina of Resonance Implementation and Release Plan

**Status** · Draft

Ship one physically modeled ocarina synthesizer as a Windows x64 VST3, a standalone GitHub Pages instrument, and a native Android app for phones and tablets. All three builds will use the same C++ sound engine, 12 finger-hole controls, 40 factory presets, effects, and performance tools. The Android release includes a directly installable signed APK and a signed Android App Bundle for potential Google Play distribution.

The sound ranges from expressive ceramic vessel flute to rounded late-1990s fantasy tones and experimental electronics. Use original synthesis, melodies for demonstrations, and artwork. The instrument generates its sound in real time without prerecorded ocarina samples or external audio assets.

## Plugin family conventions

- **Eight Ball** is the reference for JUCE/CMake, the C++ to WebAssembly build, the AudioWorklet, versioned JSON presets, and Windows build and packaging scripts. **E-Piano** is a reference for consistent controls, preset browsing, keyboard feedback, and a browser instrument available through GitHub Pages. [Eight Ball](https://github.com/BlinkZer0/8-Ball) · [E-Piano](https://github.com/BlinkZer0/E-PianoVST)
- **No Strings** and **Brass Knuckles** are references for physical-instrument controls, level-matched presets, mobile layouts, USB and Bluetooth MIDI behavior, and Android build commands. Preserve those family conventions in the Compose interface and use Oboe for native Android audio. [No Strings](https://github.com/BlinkZer0/No-Strings) · [Brass Knuckles](https://github.com/BlinkZer0/Brass-Knuckles)
- **Pedal Faster** is the reference for effects packaging, preset format documentation, repeatable renders, and release validation. Retain familiar `.bat` and PowerShell entry points for Windows development. [Pedal Faster](https://github.com/BlinkZer0/Pedal-Faster)

## Shared architecture

Use a framework-independent C++20 core built with CMake. JUCE links it into the VST3, Emscripten builds it for the browser, and the Android NDK links it into the ARM64 native library. Platform adapters translate audio buffers, timestamped notes and controller events, transport, parameters, and preset snapshots into the shared engine. Kotlin and TypeScript own platform integration and UI. The native core owns synthesis, effects, parameter validation and smoothing, voice allocation, and sequencer playback.

| Target | Platform integration | Distribution |
| --- | --- | --- |
| Windows x64 | JUCE VST3 processor and state adapter. Evaluate the family's WebView2 editor pattern in FL Studio during the first milestone. | VST3 bundle in a ZIP, with install and rescan instructions. |
| Browser | TypeScript UI, Web Audio, and an AudioWorklet with its own single-threaded instance of the shared WebAssembly module. | Complete static build in `docs/` with a working `index.html`, also distributed as a ZIP. |
| Android | Kotlin and Compose with a narrow JNI bridge, NDK/CMake and Oboe audio, and Android MIDI and file APIs. | Debug APK for development. Signed `arm64-v8a` APK and signed AAB for release. |

- Define stable parameter IDs, ranges, units, curves, defaults, and automation mappings once. Generate bindings for desktop, web, and Android. Share original artwork, theme tokens, and normalized hole positions across renderers. Commit one `VERSION` source for plugin metadata, web build metadata, Android `versionName`, and release filenames. Track a monotonically increasing Android `versionCode` and keep the preset schema version independent.
- Preallocate voices, effects, and event buffers. Use bounded real-time-safe event queues and hand off validated preset snapshots at block boundaries. Keep allocation, locks, blocking calls, logging, and file I/O out of the audio callback. Smooth continuous controls and fade note, steal, bypass, preset, and stream transitions. Keep meters and animation on the UI side.
- Track note ownership by source and channel so keyboard touches, MIDI devices, and the sequencer can coexist. Handle queue overflow with a bounded recovery path that clears held notes. Release notes when a source disappears or a gesture is cancelled.

| Path | Contents |
| --- | --- |
| `CMakeLists.txt`, `VERSION`, `README.md`, `LICENSE` | Shared build entry, release version, build and usage instructions, and licensing. |
| `src/dsp/`, `src/shared/`, `src/plugin/` | Air jet, vessel resonator, voices, modulation, and effects. Shared parameters, events, presets, and sequencer. JUCE host adapter. |
| `ui/`, `presets/` | Shared art, components, themes, control geometry, and the four canonical factory banks. |
| `android/`, `web/` | Complete Android Studio project and Windows/Unix Gradle wrappers. TypeScript and WebAssembly build sources. |
| `docs/`, `design/` | Generated Pages deployment. DSP architecture, preset format, verification instructions, and benchmark results. |
| `tests/`, `scripts/`, `.github/workflows/` | Test fixtures and renders, build/package scripts, and CI, Pages, and release workflows. |

## Sound engine and effects

Develop a reduced-order vessel-flute model with a nonlinear air jet coupled to a damped cavity resonator. Use the Helmholtz relation `f = (c / 2π) × √(A / (V × L_eff))` as a tuning basis. Model cavity volume, effective hole area and neck length, compliance, acoustic mass, and radiation losses. Let MIDI set the musical pitch while the physical controls shape timbre, response, and limited pitch movement. Calibrate weak excitation, onset, and sustained oscillation across registers.

- Model pressure, jet velocity, edge interaction, turbulence, shaped breath noise, and velocity-sensitive attacks. Add controlled breath variation, vibrato, pitch drift, and small resonator instabilities. Tune legato breath transitions and portamento. Verify stable tuning at 44.1, 48, 88.2, and 96 kHz and prepare the engine at the negotiated rate on Android and web.
- Provide four selectable modes. **Ceramic Authentic** prioritizes natural vessel-flute behavior. **Retro Fantasy** uses a rounded, stable synthesized tone. **Crystal Resonator** emphasizes glassy harmonics. **Electronic Hybrid** combines physical excitation with optional synthetic and nonlinear processing.
- Provide a monophonic legato mode and a configurable polyphonic mode designed for at least 16 voices. Use predictable voice stealing, denormal prevention, bounded feedback, and selective oversampling where measurements justify it. Expose breath and velocity curves, turbulence spectrum, excitation nonlinearity, damping, detuning, formants, harmonic enhancement, transient articulation, pitch bend range, aftertouch response, and optional modulation routing. Support CC2 breath, CC11 expression, CC64 sustain, pitch bend, aftertouch where available, modulation, and assignable CCs.
- Include room, hall, temple, cavern, and shimmer reverb. Include stereo and ping-pong delay with tempo sync, feedback, and filtering. Add chorus with width, rate, depth, and mix, plus tube warmth, retro digital coloration, soft clipping, parametric filtering, tremolo, phaser, bitcrushing, and sample-rate reduction. Give each effect independent bypass and verify output headroom and preset gain compensation.

## Instrument interface and performance

Build the interface around a large glossy royal-blue ceramic ocarina with gold Celtic knotwork, engraved spirals, and filigree against a midnight-blue background. Use scalable vector or layered assets for crisp rendering. The top strip includes preset and category selection, previous and next, save and rename, master volume, level meter, current MIDI note, and voice count. Side panels expose breath, articulation, polyphony, effects, and modulation. Keep the playable keyboard visible by default and the piano roll expandable. Use subtle airflow, resonance, and gold-ring animations with reduced-motion support and readable static states.

### Finger-hole controls

Arrange eight main openings and two smaller subholes on the face of a transverse ocarina. Place the remaining two controls below it as a matching thumb pair, representing the two underside holes. Use the following parameter assignments.

| Hole | Parameter | Hole | Parameter |
| --- | --- | --- | --- |
| 1 | Breath pressure | 7 | Aperture neck length |
| 2 | Jet velocity | 8 | Resonator damping |
| 3 | Edge interaction | 9 | Resonator detuning |
| 4 | Turbulence | 10 | Vibrato depth |
| 5 | Cavity volume | 11 | Attack articulation |
| 6 | Open-hole area | 12 | Formant color |

Give each hole an engraved label, a radial gold indicator, and a live readout with name, value, and units. On desktop and web support vertical or radial drag, wheel adjustment, and double-click reset. Provide MIDI CC mapping and VST3 host automation for every meaningful continuous parameter, including the holes. On touch, give every hole a distinct hit area, capture each active pointer, and provide fine drag and an accessible value editor with reset. Keep scrolling outside active instrument gestures so players can adjust several holes or hold keys and adjust tone simultaneously.

### Keyboard and piano roll

- Provide four visible octaves on roomy layouts, horizontal navigation, octave shifts, external MIDI note highlighting, velocity-sensitive input where feasible, sustain, bend, and modulation. Support typing-keyboard input on web and simultaneous notes on Android. Offer a full-screen Android keyboard and adapt key width and navigation for compact portrait screens.
- Support drawing and erasing notes, dragging, duration resizing, velocity editing, grid snapping and resolution, tempo, looping, play, stop, rewind, clear, and Standard MIDI File import and export where supported. The phone and tablet piano roll can take over the full screen. Preserve the sequence and instrument settings across layout changes.
- Schedule playback against the audio clock. Web and Android own their local transport. VST3 auditions independently when idle and follows available host tempo and position when synced. Use one active clock and handle stop, seek, loop wrap, and host restart without double-triggering or leaving hanging notes.

## Native Android audio and MIDI

- Open an Oboe callback stream using AAudio where supported, with OpenSL ES fallback. Request low-latency output, use the route's native sample rate, prefer exclusive sharing where supported, and retry in shared mode as needed. Expose **Low Latency**, **Balanced**, and **Stable** modes plus an adjustable buffer measured in device bursts. Show the effective buffer, rate, and backend. Let users set mono/polyphony and an active voice limit independently of the selected preset. [Android low-latency audio guide](https://developer.android.com/games/sdk/oboe/low-latency-audio)
- Serialize stream open, reconfiguration, and close on a control thread. React to output device changes and disconnects, reopen with the new stream properties, and reinitialize DSP off the audio callback. Handle audio focus loss, interruptions, backgrounding, and resume with controlled fades, transport pause, and held-note cleanup. Keep the selected preset and sequence across recovery. [Oboe full guide](https://github.com/google/oboe/blob/main/docs/FullGuide.md)
- Use `MidiManager` for class-compliant USB MIDI and supported Bluetooth LE MIDI devices. Request Bluetooth permissions when the user enables discovery. Handle device add/remove, port closure, fragmented messages and running status, and then clear only the disconnected device's notes and sustain state. Reopen compatible devices when they return. Measure Bluetooth MIDI input separately from Bluetooth audio output latency. [Android MIDI reference](https://developer.android.com/reference/android/media/midi/package-summary)
- Support compact portrait, landscape, and tablet layouts with the complete instrument, effects, and presets. Use app-local storage and the system document picker for user preset and MIDI file exchange. Bundle all required assets so first launch, sound, sequencing, and local saving work offline without an account or subscription.

## Presets and compatibility

Define one versioned JSON instrument format with stable parameter and preset IDs, a name, category, short description, synthesis parameters, effects, gain compensation, and performance/articulation settings. Keep device-specific audio, UI, and file preferences in local settings. VST3 project state embeds the portable instrument snapshot with host-specific state stored separately. Parse and migrate preset files off the audio thread, validate before applying, and keep the active patch intact when import fails.

Package the same canonical preset records on all targets and validate the IDs and content during the build. Expose the same factory catalog and user save, rename, load, import, and export operations. Store user patches in `Documents/Ocarina of Resonance/Presets` on Windows, browser-local storage, or Android app storage. Use the same portable file in all six transfer directions.

| Bank | Factory presets in order |
| --- | --- |
| Authentic · 01–10 | Classic Blue Ceramic, Alto C Ocarina, Soprano C Ocarina, Bass Ocarina, Warm Clay Vessel, Concert Ceramic, Wooden Vessel Flute, Breath of the Highlands, Ancient Folk Ocarina, Soloist Expressive |
| Retro · 11–20 | Hero's Blue Ocarina, Time Traveler, Forest Melody, Sacred Temple, Moonlit Serenade, Lost Woods, Ancient Kingdom, Fairy Fountain, Twilight Echo, Eight-Bit Dreamer |
| Electronic · 21–30 | Syntharina, Cyber Flute, Neon Breath, Resonant Pulse, Vaporwave Vessel, Glitch Ocarina, FM Airglass, Ambient Drone, Granular Illusion, Digital Spirit |
| Cinematic · 31–40 | Celtic Dawn, Druid's Lament, Mystic Cathedral, Frozen Kingdom, Etherwind, Dark Ritual, Celestial Choir, Desert Wanderer, Astral Passage, The Last Breath |

Program the entries for distinct musical uses with intentional voice, articulation, and effects settings. Level-match with representative renders and verify instant switching and deterministic behavior where appropriate. Build audible FM processing into **FM Airglass** and grain-based processing of the live modeled signal into **Granular Illusion** so the names describe the actual signal path.

## Build and release

1. **Build all three targets in CI.** On pull requests run Windows VST3 build and validation, the browser build with a static-subpath smoke test, and the Android debug Gradle build. Provide `build.bat`, `start.bat` and `serve.ps1` for the family-style Windows and web entry points, and `android/build.bat` over the Gradle wrapper. Keep the Gradle wrapper and toolchain pins under version control. Document the Android Studio import, SDK/NDK requirements, debug install steps, and Gradle tasks `assembleDebug`, `assembleRelease`, and `bundleRelease`.
2. **Deploy the browser build.** Generate a complete `docs/` from `web/` and publish the same static output through GitHub Pages. Use relative or base-aware URLs for the worklet, Wasm, and assets. Start audio after a user gesture, show actionable errors for unsupported or failed initialization, and keep the default worklet path compatible with static hosting without custom cross-origin isolation headers. Use no synthesis server or paid API. [Emscripten AudioWorklet documentation](https://emscripten.org/docs/api_reference/wasm_audio_worklets.html) · [GitHub Pages workflow](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages)
3. **Build signed Android packages for release tags.** Build the ARM64 APK and AAB using a user-provided signing key through protected release-environment secrets or an ignored local signing configuration. Keep keystores and passwords out of source control, logs, caches, and artifacts. Use a temporary CI keystore file and remove it after signing. Verify the APK signer, AAB signature and contents, and native library ABI and 16 KB page alignment. Test updates with the same APK signer. Keep the AAB ready for a possible Play upload and document how the direct APK signing key relates to any future Play app signing and upload keys. Verify direct installation under current Android developer verification rules for the intended distribution regions. [Android app signing](https://developer.android.com/studio/publish/app-signing) · [Android 16 KB page sizes](https://developer.android.com/guide/practices/page-sizes) · [Android developer verification](https://developer.android.com/developer-verification)
4. **Publish one coordinated GitHub Release.** Match tag `vX.Y.Z` to `VERSION`, verify the three platform builds and their evidence, and promote the tested Pages output from the same commit. Require the applicable signing credentials and successful signature checks before publishing release artifacts. Keep debug artifacts in CI. Attach checksums and release notes identifying verified hosts, browsers, Android devices, and MIDI hardware. Use the following filenames.

   - `Ocarina-of-Resonance-Windows-vX.Y.Z.vst3.zip`
   - `Ocarina-of-Resonance-Android-vX.Y.Z.apk`
   - `Ocarina-of-Resonance-Android-vX.Y.Z.aab`
   - `Ocarina-of-Resonance-Web-vX.Y.Z.zip`

## Implementation milestones

| Milestone | Exit condition |
| --- | --- |
| 1. Architecture and playable prototype | Establish the shared parameter, event, and preset contracts. Build the same minimal test voice through JUCE, the deployed worklet, and an Oboe debug APK installed on a physical ARM64 device. Prove that each normal input path can produce sound. Choose the Android minimum SDK and reference phone/tablet, the primary DAW and browsers, and the stable VST3 and Android identifiers. Confirm that dependency and asset licenses allow distribution across all three targets. |
| 2. Physical synthesis | Deliver all four modes, calibrated tuning, mono/legato and polyphonic voice management, and repeatable DSP validation. Run stress renders on native and Wasm builds and profile the core on physical Android hardware. |
| 3. Instrument UI | Deliver original art, 12 connected finger holes, parameter automation, meters, preset and effects panels, and accessible desktop, phone, and tablet interaction. |
| 4. Effects and presets | Deliver the complete effects chain, exactly 40 benchmarked factory patches, and portable user preset import/export. Verify sound and parameter parity with shared test events. |
| 5. Keyboard, sequencer, and MIDI | Deliver keyboard and piano roll on all builds, host transport integration, Android full-screen performance views, and USB and BLE MIDI tests on supported hardware. |
| 6. Packaging and deployment | Produce the Windows bundle, static `docs/` and live Pages deployment, debug APK, and signed APK/AAB once credentials are available. Verify that the tagged workflow packages one common version. |
| 7. Release validation | Complete the acceptance checks below. Deliver source, builds, setup and user documentation, DSP and preset specifications, automated tests, benchmark reports, and representative renders where practical. |

## Acceptance and verification

1. **Windows.** Build the x64 VST3, pass VST3 validation, and install and play it in FL Studio. Verify a second compatible host for scanning, MIDI, automation, state recall, bypass/reset, sample-rate changes, and host-synced audition.
2. **Browser.** Open the actual HTTPS GitHub Pages project URL and play from the deployed worklet. Test the project subpath, audio startup gesture, unsupported-feature messages, preset transfer, typing keys, touch, and piano roll. Exercise browser layouts and confirm the site plays without a synthesis server.
3. **Android install and sound.** Install the release APK through the normal installer on a compatible physical ARM64 device. Launch in airplane mode and play the instrument and effects. Verify portrait, landscape, tablet layout, clean install, and update from an earlier build signed with the same key. Exercise the AAB on a device via the generated APK set.
4. **Factory presets.** Validate and render all 40 on all three builds. Check catalog IDs and values, gain, silence, excessive peaks, NaN/Inf output, and recall while notes are playing. Compare native and Wasm tuning, envelopes, and output within measured tolerances. Listen for authentic vessel tone, rounded retro tone, soft and forceful attacks, releases, legato, vibrato, drift, and electronic transformations.
5. **Finger holes.** Verify all 12 respond end to end on desktop and mobile. Exercise reset, fine adjustments, MIDI mapping, host automation, and simultaneous touches without unintended scrolling.
6. **Android keyboard and piano roll.** Play multitouch chords, use sustain and full-screen views, edit a loop, and verify play, stop, seek, and rotation without losing the sequence or leaving notes held.
7. **Real-time performance.** Automate MIDI tuning across octaves, stable note generation, envelopes, smoothing, resonator stability, polyphony, voice stealing, supported sample rates, stuck-note recovery, and Wasm initialization. On the reference Android phone and tablet, log callback timing and underruns where available at the route's negotiated rate. Exercise AAudio, shared-mode retry, and OpenSL ES fallback. The proposed baseline for speaker and wired output in Balanced mode is 16 active voices with a representative effects chain, a 99th-percentile callback time under 70% of its deadline, zero steady-state underruns in a 10-minute run where measurable, and no audible recurring glitches. Set a note-to-sound latency target using measurements from the first hardware prototype. Publish exact hardware, routes, settings, and results for all latency modes, interruptions, and route switches.
8. **Android controllers.** Play notes and verify expressive CCs, sustain, pitch bend, and hot unplug or disconnect followed by reconnect on compatible USB and Bluetooth LE MIDI hardware. Confirm disconnected controllers leave no hanging notes and other sources remain playable.
9. **Portable presets.** Save and export on each platform, then import and audition on both other platforms. Compare restored synthesis, effects, and performance parameter values. Verify schema migration, invalid-file handling, and VST3 project-state restoration independently.

Record the command or reproduction steps and the artifact commit for every release check. If a toolchain, signing input, DAW, or physical device is unavailable, complete the independent checks and record the remaining gate as pending. Release completion requires verified builds and the physical device results above.
