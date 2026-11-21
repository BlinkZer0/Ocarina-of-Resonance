# Phase 5 Keyboard, Sequencer, and MIDI

Version 0.5.0 shares a piano roll and full-screen performance views across Windows VST3, web, and Android. The instrument supports multitouch chords, pointer velocity, local sustain, and simultaneous key highlighting. The browser also maps the typing keyboard to notes.

## Piano roll

Expand **Piano roll** to edit one to 64 bars of 4/4, with up to 512 notes. Draw with a click or drag across the grid. Drag a note or its right edge to move or resize it. Set the selected note's velocity in the toolbar. **Erase** and right-click remove notes. **Scroll** lets touch input pan without editing. Focused notes respond to arrow keys, Shift plus left or right for length, and Delete. The ruler seeks with a click or arrow keys.

Play and stop control the local transport. Rewind returns to the first beat and Loop repeats the configured bar range. In the VST3, enable **DAW sync** to follow host quarter-note position and tempo while the DAW plays. Local audition remains available when the host is idle. A host stop or seek releases the roll's notes independently of other input sources. Delay uses the local piano-roll tempo during audition and the host tempo when following it.

**Import MIDI** accepts Standard MIDI File format 0 and 1 with quarter-note timing. The editor uses the first tempo and flattens note tracks onto one 4/4 roll. **Export MIDI** writes a format 0 file with velocity, pitch, note timing, tempo, and trailing bars. The browser keeps the current roll in local storage, Android uses app preferences, and the VST3 stores it in project state.

## MIDI and Android audio

The C++ MIDI dispatcher supports note velocity, sustain, modulation, breath, expression, channel pressure, polyphonic pressure, and pitch bend. Each port owns its held notes and controller state. The browser uses Web MIDI when granted access. The VST3 receives controller events from its DAW track and applies learned mappings even with the editor closed. Android uses MidiManager for byte-stream ports and scans specifically for BLE MIDI after the user requests discovery. The byte-stream parser handles fragmented messages, running status, and interleaved real-time bytes. [Web MIDI](https://www.w3.org/TR/webmidi/) · [Android MIDI receiver](https://developer.android.com/reference/android/media/midi/MidiReceiver)

Android offers Low Latency, Balanced, and Stable modes with automatic buffers of two, three, and five bursts respectively. An explicit setting selects one to eight bursts within the device's limits. Low Latency prefers exclusive sharing, with shared and OpenSL ES fallback if necessary. Android stops playback on focus loss, reopens on stream disconnection, and retains the instrument and piano-roll settings. It reports the negotiated sample rate and effective buffer. [Android audio focus](https://developer.android.com/media/optimize/audio-focus) · [Oboe latency guide](https://developer.android.com/games/sdk/oboe/low-latency-audio)

## Timing and verification

The C++ sequencer uses 960 ticks per quarter note on all three platforms. A three-buffer handoff publishes validated pattern edits, and the audio renderer schedules sorted edges against the local sample clock or host position. Adjacent gates articulate separately and overlapping notes of the same pitch merge into one gate. The sequencer owns its notes separately from the keyboard and MIDI ports.

Native tests cover exact onset and release, looping, host seeks and stops, and source isolation. Browser tests cover draw, resize, velocity, playback, stop, rewind, full-screen relayout, MIDI file transfer and storage, typing-keyboard input, Web MIDI mappings, and disconnect cleanup.
