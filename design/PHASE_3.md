# Phase 3 Instrument Interface

Version 0.4.1 follows the traditional transverse layout. Eight main holes and two smaller subholes sit on the glazed face. Attack Articulation and Formant Color sit just below it as thumb controls. The front recesses and their gold indicators share the positions in `ui/holes.json`, which keeps the visible openings and touch targets aligned.

Version 0.3.0 uses a shared instrument panel for the VST3, the GitHub Pages build, and Android. The 12 dial centers are measured from the drilled finger holes in the artwork and stored as percentages of its dimensions. The bezel overlays share those centers at every panel size. On narrow screens, the stage preserves distinct touch targets and permits panning at the outer edges.

The C++ parameter registry supplies IDs, names, units, defaults, and ranges to the generated UI manifest. Each hole supports independent pointer capture, vertical drag, fine adjustment, keyboard steps, a numerical editor, and reset. On Windows, the editor forwards begin and end gestures and values to the DAW parameter system, and reads automated values back into the rings. The internal keyboard, voice count, last note, level meter, and airflow detail follow the active engine. Reduced motion suppresses the airflow animation.

- **Windows:** JUCE embeds the panel and uses WebView2. Virtual-keyboard events reach the C++ engine through the processor's bounded queue. [JUCE WebBrowserComponent](https://docs.juce.com/master/classjuce_1_1WebBrowserComponent.html)
- **Web:** The static panel sends values and note events to the shared C++ engine in its Wasm worklet.
- **Android:** The APK serves bundled UI assets from a local WebView origin, and a Kotlin bridge queues parameter and note events for the native JNI and Oboe engine. [Android in-app content](https://developer.android.com/develop/ui/views/layout/webapps/load-local-content)

The display includes the four sound models, live breath and articulation controls, master gain, and the playable four-octave keyboard. The factory preset browser and effects panels belong to Phase 4. Sequencing and external MIDI CC mapping belong to Phase 5.

## Artwork

The built-in image generation tool produced the original 1774 × 887 transparent artwork. The final cutout is stored as `ui/ocarina.webp`, and `ui/holes.json` contains the measured opening centers. The optimized art is bundled with the desktop, mobile, and web interfaces.

Final artwork edit prompt

> Use case: precise-object-edit. Edit the supplied cutout of a cobalt blue ocarina. Keep the entire ornate ceramic instrument, original silhouette, wide two-to-one canvas, angle, upper-right mouthpiece, glossy blue glaze, gold Celtic filigree, transparent alpha outside the instrument, lighting, and framing. Change only the broad blue face by physically drilling exactly twelve dark, round finger openings in the ceramic, six in an upper row and six in a lower row. These must be actual hollow recesses with deep navy shadowed interiors and a fine shaded ceramic rim on each opening. Leave the gold border untouched. The openings are widely spaced and evenly distributed across the blue chamber like holes on a real ocarina. Upper row center positions at approximately 14, 28.5, 43, 57, 71.5, and 86 percent of canvas width and 51 percent of canvas height. Lower row same six center positions and 74 percent of canvas height. Each opening about 4 percent of canvas width in diameter. Leave the instrument rim intact. Do not add controls, knobs, interface overlays, words, or extra holes. Keep the exterior background genuinely transparent.

## Repeat browser verification

Run `npm ci`, `npm run build`, and `npm test` from `web/` with the supported browser installed. The functional tests check the rendered dial centers against the artwork, exercise all twelve parameters, verify that breath changes the sounding voice, and exercise independent touches, fine adjustment, and keyboard navigation at a phone viewport.

## Verification results

- The tested desktop, portrait phone, landscape phone, and tablet layouts kept all twelve knobs centered over the drilled openings. Touch targets stayed distinct at viewport widths down to 320 CSS pixels.
- In REAPER 7.86, the embedded editor sent a breath-pressure gesture to the host, reflected a host automation change on the dial, and played its on-screen keyboard through the native engine. The VST3 also passed all 47 Steinberg validator checks.
- The hosted build served byte-identical artwork, parameter metadata, and Wasm. Edge verified that the breath dial changed a held note and that releasing the key cleared it. [Live instrument](https://blinkzer0.github.io/Ocarina-of-Resonance/)
- The ARM64 debug APK passed signature and alignment checks and installed on the Samsung SM-S948U1. Final on-device touch and audio verification is pending while the phone is locked.
