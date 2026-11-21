# Ocarina of Resonance 0.6.0

Play the physically modeled blue-and-gold ocarina with four sound models, 40 factory presets, and 11 stereo effects. Edit and exchange MIDI patterns in the piano roll and share `.orpreset` files across Windows, web, and Android.

- **Windows** — Extract the entire VST3 bundle into a folder your DAW scans. The instrument editor uses Microsoft Edge WebView2.
- **Android** — The APK installs directly on compatible ARM64 phones and tablets running Android 8 or later. The AAB is for Play distribution.
- **Web** — Play on GitHub Pages or serve the extracted web ZIP from HTTPS or localhost.

Verification covers FL Studio and REAPER playback, REAPER transport sync, Chromium playback and MIDI file exchange, and AAudio playback on a Samsung SM-S948U1. All 40 factory presets rendered on Windows, WebAssembly, and ARM64 Android. Android MIDI disconnect behavior was checked with a virtual Android MIDI port. Physical USB and Bluetooth MIDI controller checks are pending. [Release validation](https://github.com/BlinkZer0/Ocarina-of-Resonance/blob/main/design/PHASE_7.md) · [Play on GitHub Pages](https://blinkzer0.github.io/Ocarina-of-Resonance/)
