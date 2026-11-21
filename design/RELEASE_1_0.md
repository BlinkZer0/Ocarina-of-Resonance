# Ocarina of Resonance 1.0.0 release checklist

Version `1.0.0` is shared by the VST3, web build, and Android APK. Android version code is 8. The hardware candidate is tagged `v1.0.0-rc.1` at commit `d199b96`. The GitHub rehearsal passed the Windows, web, and Android debug builds and verified the Windows and web archive checksums. [Candidate source](https://github.com/BlinkZer0/Ocarina-of-Resonance/tree/v1.0.0-rc.1) · [GitHub rehearsal artifacts](https://github.com/BlinkZer0/Ocarina-of-Resonance/actions/runs/44949725028#artifacts)

## 1. Sign and verify Android

A local 1.0.0 rehearsal used a disposable QA key. The standalone APK and the universal APK generated from the AAB passed ARM64 checks, 16 KB alignment, and certificate verification. The standalone APK installed with ADB and launched on the Samsung SM-S948U1. The APK set installed over it with the same signing certificate and launched. The QA key and installed QA package were removed after the test.

The 1.0.0 benchmark build cold-started on the Samsung in airplane mode with no active default network. It loaded all 40 factory presets and the 12 hole controls, opened AAudio on the built-in speaker at 48 kHz, and rendered 24,071 non-silent frames for its A4 test. The original airplane mode, Wi-Fi, and Bluetooth settings were restored.

The 1.0.0 debug APK updated the existing 0.6.0 debug installation. Android reported version code 8 and the A4 test rendered 21,857 non-silent frames.

The production signing environment still needs the owner's four credentials. With those configured, run the signed candidate workflow, then repeat direct installation through Android's package installer, offline playback, and the bundletool update with the production signer. [Signed Android candidate](PHASE_6.md#signed-android-candidate)

## 2. Finish hardware and listening checks

The 1.0.0 VST3 passed all three Windows automated tests and 47 Steinberg validator checks. REAPER loaded the 1.0.0 editor, played its keyboard, and verified Breath Pressure automation in both directions. The 1.0.0 browser build passed 10 tests. The extracted web ZIP imported a MIDI pattern at the Pages project subpath, played through the Wasm worklet, and stopped without leaving notes held.

On the Samsung, the 1.0.0 benchmark played over Bluetooth A2DP at 44.1 kHz. The phone disconnected Bluetooth during a note, switched to the speaker at 48 kHz, released the note, and played a fresh note. The phone used A2DP again on a subsequent app launch after Bluetooth was re-enabled. Automatic return within the same session remains to be checked.

Physical USB MIDI, Bluetooth LE MIDI, wired audio, a tablet, note-to-sound latency, and listening sign-off remain open. The earlier validation records the preset transfers, FL Studio playback, and the 10-minute Bluetooth and speaker audio tests. [Phase 7 validation](PHASE_7.md) · [Phase 8 hardware measurements](PHASE_8.md)

## 3. Publish 1.0.0

Confirm the applicable JUCE 8 distribution license. After the signed candidate and remaining acceptance checks pass, tag the verified source commit `v1.0.0`. Check the four GitHub Release downloads and their checksums, and verify live Pages playback from that deployment. [Distribution notices](../licenses/README.md) · [Stable release workflow](PHASE_6.md#publish-the-stable-release)
