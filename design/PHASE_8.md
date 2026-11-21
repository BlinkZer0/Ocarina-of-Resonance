# 0.6.0 release preparation

Phase 7 established Windows, web, and Android playback and preset compatibility. This follow-up covers the Samsung's physical Android audio routes and the packages intended for distribution. Build commit `1b61fc9` added the package notices and Android route diagnostics. Commit `1991c64` made the updated web module compatible with a cached older web panel. Commit `1656f3f` fixed checkout line endings for cross-platform notice verification. [Phase 7 evidence](PHASE_7.md) · [Packaging rehearsal](https://github.com/BlinkZer0/Ocarina-of-Resonance/actions/runs/44937598240)

## Android controls and audio

On the Samsung SM-S948U1, injected multitouch events moved two finger-hole controls independently without scrolling the page. Two additional touches played a chord in the full-screen keyboard. Sustain held both voices after liftoff, and switching it off released both. Sending the app to the background during a chord and reopening it cleared the voices and allowed a new chord to play.

The table shows four-second profiles with 16 voices and the Classic Blue Ceramic preset through Bluetooth A2DP. The optimized benchmark used AAudio at 44,100 Hz with 882 frames per burst. Each mode change reopened the stream.

| Mode | Buffer | Callback p99 | Peak | Underruns |
| --- | ---: | ---: | ---: | ---: |
| Low Latency, automatic | 1,764 frames | 17% | 23% | 0 |
| Balanced, automatic | 2,646 frames | 17% | 23% | 0 |
| Stable, automatic | 4,410 frames | 18% | 22% | 0 |
| Stable, four bursts | 3,528 frames | 18% | 22% | 0 |

The forced OpenSL ES backend ran at 48,000 Hz and reported 23,975 non-silent frames for the A4 test. It rendered 199 callbacks during the four-second 16-voice profile with 12% p99, a 12% peak, and zero reported underruns. Automatic selection of OpenSL ES after a failed AAudio open still needs a separate check.

The built-in speaker negotiated AAudio at 48,000 Hz with a 288-frame buffer and 96 frames per burst in Balanced mode. A four-second Mystic Cathedral profile held 16 voices for 2,001 callbacks at 28% p99, 37% peak, and zero reported underruns.

The final speaker run held 16 voices for 600 seconds and 299,999 callbacks with Mystic Cathedral at 2% Master. Callback p99 was 27%, the peak was 66%, and AAudio reported zero underruns. This meets the planned numerical target for the speaker route.

The first run was interrupted after 2 minutes and 42 seconds by a system Bluetooth device disconnect. Android closed the MMAP stream and the app started a new AAudio stream on the speaker about 0.33 seconds later. The second run stopped after more than five minutes when Android paused the app's audio.

### Repeat the route tests

Build `android/build.bat assembleBenchmark` and install the separate `.benchmark` APK. Launch the app with the corresponding command, then open **Audio diagnostics**. Use **Balanced** and **Automatic**. For the 10-minute comparison, select **Mystic Cathedral**, set Master to 2%, and choose **Profile 16 voices for 10 min**.

```powershell
adb -s <device> shell am start -S -n com.blinkzer0.ocarina.benchmark/com.blinkzer0.ocarina.MainActivity --ez testSpeaker true
adb -s <device> shell am start -S -n com.blinkzer0.ocarina.benchmark/com.blinkzer0.ocarina.MainActivity --ez testOpenSLES true
```

Use the speaker and OpenSL ES launch options on separate runs. The speaker option requests the built-in output for that app's AAudio stream. Check the active route using `adb shell dumpsys media.audio_policy` and confirm the panel's backend. Both options require a debuggable package.

## Distribution packages

The Windows ZIP includes the Steinberg VST3 and Microsoft WebView2 loader notices. The Android APK and AAB include Oboe, Kotlin, libc++, and libc++abi license material. The packager verifies the notices against the repository copy. A disposable QA key produced an APK and AAB with the same signer, version 0.6.0, version code 7, and 16 KB alignment. [Notice sources](../licenses/README.md)

The current build passed three Windows tests and 10 browser tests. The public Pages deployment served the same JavaScript as commit `1991c64` and played an imported MIDI pattern through the Wasm worklet. The new browser test also starts audio with the previous panel markup. The final GitHub rehearsal completed all three builds and collected the verified Windows and web ZIPs and the separate Android debug APK. The rebuilt debug APK updated the installation on the Samsung, opened AAudio, and rendered 21,941 non-silent frames for the A4 test. [Pages deployment](https://github.com/BlinkZer0/Ocarina-of-Resonance/actions/runs/44937065920) · [Rehearsal artifacts](https://github.com/BlinkZer0/Ocarina-of-Resonance/actions/runs/44937598240#artifacts)

## Remaining release checks

- Configure the owner's signing credentials in the protected GitHub environment. Build a production-signed candidate from a candidate tag, exercise direct installation and the generated bundletool APK set on hardware, and verify offline playback before tagging the verified commit. [GitHub signing environment](https://github.com/BlinkZer0/Ocarina-of-Resonance/settings/environments) · [Signed Android candidate](PHASE_6.md#signed-android-candidate)
- Exercise physical USB MIDI and Bluetooth LE MIDI with a compatible controller, including sustained notes, expressive controls, disconnect, and reconnect. Wired audio, the physical tablet, route switches, and note-to-sound latency still need hardware verification.
- Confirm the applicable JUCE 8 distribution terms and complete listening sign-off. [JUCE licensing](https://juce.com/legal/juce-8-licence/)
