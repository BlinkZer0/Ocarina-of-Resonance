#include "dsp/VoiceManager.h"

#include <emscripten/emscripten.h>

namespace {
ocarina::VoiceManager voice;
constexpr int capacity = 256;
float samples[capacity];
}

extern "C" {
EMSCRIPTEN_KEEPALIVE void prepare(float rate) { voice.prepare(rate); }

EMSCRIPTEN_KEEPALIVE void note(int on, int key, int source) {
    if (key >= 0 && key <= 127 && source >= 0 && source <= 65535) {
        voice.apply({on ? ocarina::EventType::noteOn : ocarina::EventType::noteOff,
            static_cast<uint16_t>(source), 0, static_cast<uint8_t>(key), 0.8f});
    }
}

EMSCRIPTEN_KEEPALIVE void release() { voice.releaseAll(); }

EMSCRIPTEN_KEEPALIVE void parameter(int index, float value) { voice.setParameter(static_cast<size_t>(index), value); }
EMSCRIPTEN_KEEPALIVE int activeVoices() { return voice.soundingVoices(); }
EMSCRIPTEN_KEEPALIVE int latestNote() { return voice.latestNote(); }

EMSCRIPTEN_KEEPALIVE void performance(int model, int voices) {
    voice.setParameter(ocarina::model, static_cast<float>(model));
    voice.setParameter(ocarina::voiceLimit, static_cast<float>(voices));
    voice.setParameter(ocarina::voiceMode, voices == 0 ? 0.0f : 1.0f);
}

EMSCRIPTEN_KEEPALIVE float* render(int count) {
    for (int index = 0; index < count && index < capacity; ++index)
        samples[index] = voice.nextSample();
    return samples;
}
}
