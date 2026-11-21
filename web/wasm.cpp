#include "dsp/PrototypeVoice.h"

#include <emscripten/emscripten.h>

namespace {
ocarina::PrototypeVoice voice;
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

EMSCRIPTEN_KEEPALIVE float* render(int count) {
    for (int index = 0; index < count && index < capacity; ++index)
        samples[index] = voice.nextSample();
    return samples;
}
}
