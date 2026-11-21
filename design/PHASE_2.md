# Phase 2 Physical Model and Verification

Version 0.2.0 runs the same C++ air-jet and vessel model in the VST3, the browser AudioWorklet, and the Android Oboe callback. Each note has its own cavity, jet state, breath noise, and articulation. The engine offers last-note-priority mono legato and configurable polyphony from 1 to 16 voices.

## Acoustic model

`VesselResonator` represents air compression as acoustic compliance `C = V / (ρc²)` and aperture inertia as acoustic mass `M = ρL_eff / A`. In energy-normalized coordinates the cavity pressure and aperture flow rotate at `f_H = 1 / (2π√(MC))`. MIDI provides the musical reference for that rotation. The ratio of the current to the nominal Helmholtz frequency contributes a calibrated shift capped at ±30 cents and changes radiation loss. Breath pressure, detuning, vibrato, and pitch bend add separate pitch terms. This keeps normal geometry edits playable within a tuned keyboard scale. [Kobayashi et al., ocarina sounding and cavity resonance](https://arxiv.org/abs/0911.3567)

`AirJetModel` approximates average acoustic work delivered through the edge over a cavity cycle. Blowing pressure powers the jet and the jet speed and edge settings change the effectiveness of coupling. Radiation and damping remove work from the pressure-flow state. A weak jet decays, while a strong jet builds oscillation until the energy-dependent saturation balances loss. The turbulence model supplies a seed and a band-shaped breath signal. Velocity changes drive and onset, and pressure can restart a held note after CC2 closes the jet. This reduced-order model uses calibrated cycle-averaged feedback. [Terrien et al., nonlinear flute-like instrument models](https://arxiv.org/abs/1207.7136)

The four modes set different jet, radiation, breath-noise, and drift coefficients. **Ceramic Authentic** keeps a strong fundamental and audible breath articulation. **Retro Fantasy** dials back breath and higher partials for a rounder tone. **Crystal Resonator** raises the second and third partials. **Electronic Hybrid** adds a strong fourth partial from nonlinear pressure shaping. Harmonics fade as they approach Nyquist. Oscillation and turbulence are generated per voice with no audio assets required.

Continuous controls and mode coefficients are smoothed. Mono legato reuses the active cavity and glides to the most recently held note. In poly mode a new note takes an idle slot, then a releasing slot, then the oldest active slot. Rapid reassignment feeds a short decay to the mixer to preserve continuity. Note and sustain ownership is tracked by source and MIDI channel. The VST3 exposes the 12 hole parameters and expression controls, including breath CC2, expression CC11, sustain, pitch bend, and both forms of aftertouch. Web and Android expose mode and voice count selectors for the playable prototype.

## Measured results

| Check | Result |
| --- | --- |
| Native sustained tuning | Maximum 2.40 cents absolute error across MIDI notes 36, 48, 60, 69, 84, and 96 at 44.1, 48, 88.2, and 96 kHz. Vibrato was disabled for the tuning measurement. |
| Mode spectrum at A4 | H3/H1 was 4.1% for Ceramic, 1.6% for Retro, and 17.1% for Crystal. H4/H1 was 13.8% for Hybrid and 1.7% for Crystal. |
| Native stress | Tests covered all MIDI notes at minimum and maximum synthesis settings, four sound modes, 16-voice allocation and stealing, sustained key ownership, legato return, timed release to silence, breath restart, and queue recovery. |
| Windows DSP cost | Intel Core i9-14900HX, release build, 48 kHz, 16 held notes in Electronic Hybrid. The engine rendered 10 seconds in 73.4 ms of wall time, about 0.74% of one real-time thread for this run. |
| Android callback cost | Samsung SM-S948U1 debug APK, AAudio at 44.1 kHz, four seconds with 16 held notes. Ceramic measured 388 callbacks at p99 27% of the callback deadline with peak 28%. Hybrid measured 390 callbacks at p99 27% with peak 29%. AAudio reported zero additional xruns in both runs. |
| Browser | Wasm stress exercises all four models at 44.1, 48, 88.2, and 96 kHz. The Edge AudioWorklet test verifies playback and release with a model and mono mode selected through the web UI. |

These timing results cover synthesis with the current prototype controls. Full effects, extended Android soak and latency measurements, and physical listening comparisons are later milestones.

## Reproduce and audition

From the repository root, build and run the native tests, benchmark, and four example renders.

```powershell
.\build.bat
.\build\windows\Release\ocarina_core_test.exe --benchmark
.\build\windows\Release\ocarina_core_test.exe --render build\phase-2-renders
```

The generated WAVs play the same soft and forceful attacks, legato interval, and release in all four modes at 48 kHz. To repeat the Android callback run, select the model on the debug APK, tap **Profile 16 voices**, and read the `OcarinaAudio` output in ADB logcat.
