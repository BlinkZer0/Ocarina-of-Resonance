# Phase 4 Effects and Presets

Version 0.4.0 uses the same C++ instrument and stereo effects on Windows VST3, browser Wasm, and native Android. The twelve synthesis controls still sit inside the ocarina's drilled holes. The shared panel adds a preset browser and an expandable effects rack.

## Signal path

The voice engine feeds parametric EQ, saturation, audio-rate FM, live granular processing, tremolo, phaser, stereo chorus, bitcrusher, sample-rate reduction, delay, and reverb, in that order. Each processor can be bypassed independently. Saturation has Tube, Retro digital, and Soft clip modes. Delay offers stereo or ping-pong routing, milliseconds or tempo divisions, and filtered feedback. Reverb has Room, Hall, Temple, Cavern, and octave-up Shimmer modes.

In tempo mode the VST3 uses the DAW's tempo when available. Android, the web instrument, and VST3 hosts that omit BPM use the tempo stored in the preset. The engine allocates delay and grain buffers at audio preparation and smooths live controls and bypass changes. Preset gain is applied after the effects, before the master output.

## Factory and user presets

[factory.json](../presets/factory.json) contains ten presets in each of the Authentic, Retro, Electronic, and Cinematic banks. The web build checks the forty IDs and expands abbreviated authoring records against the C++ parameter defaults. It emits a catalogue for all three targets and test vectors for the native renderer.

The [v1 preset schema](../presets/schema-v1.json) stores synthesis and effect values in native units, together with the model, voice settings, gain trim, and tempo. Import validates values and effect IDs before applying a complete snapshot. Earlier v1 files without tempo use 120 BPM. Omitted synthesis values and effect parameters use registry defaults, and omitted effects are bypassed.

Use **Save** and **Rename** for the local library, and **Export** and **Import** to exchange `.orpreset` files. The Windows editor stores user patches in `Documents/Ocarina of Resonance/Presets`. The browser uses local storage. Android uses its private app directory for the library and the system document picker for file exchange. VST3 host state embeds a v1 snapshot alongside the host's parameter state.

## Render verification

The factory benchmark plays MIDI C4 at velocity 0.8 for one second at 48 kHz and renders one second of release. It measures stereo RMS during the last half-second of the held note and checks peak level and stereo output across the full render. Each patch has its own gain trim targeting −21 dBFS RMS on that reference note.

All forty presets measured between −21.005 and −20.995 dBFS RMS across Windows x64, Wasm, and a native ARM64 renderer run on a Samsung phone. The highest sample peak was 0.279 on a 0–1 scale. The largest RMS difference for the same patch across platforms was 0.00002 dB. Other notes and polyphonic playing vary with velocity, synthesis settings, and effect tails.

`ocarina_factory` checks every processor against a dry render, verifies ping-pong timing and stereo routing, and stresses voice stacks at 44.1, 48, and 96 kHz. The VST3 host test verifies project-state restoration of synthesis parameters and a delay switch. The browser tests exercise the 40 snapshots and the UI's bank selection, effect bypass, save, rename, export, re-import, and invalid-file handling.
