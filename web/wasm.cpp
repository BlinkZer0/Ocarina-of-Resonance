#include "dsp/InstrumentEngine.h"
#include "shared/Midi.h"

#include <emscripten/emscripten.h>

namespace {
ocarina::InstrumentEngine voice;
constexpr int capacity = 256;
float samples[capacity * 2];
std::array<int32_t, ocarina::Sequencer::packetSize> sequencePacket;
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
EMSCRIPTEN_KEEPALIVE void keyNote(int on, int key, int source, float velocity) {
    if (key >= 0 && key <= 127 && source >= 0 && source <= 65535)
        voice.apply({on ? ocarina::EventType::noteOn : ocarina::EventType::noteOff,
            static_cast<uint16_t>(source), 0, static_cast<uint8_t>(key), velocity});
}
EMSCRIPTEN_KEEPALIVE void releaseSource(int source) {
    voice.apply({ocarina::EventType::releaseSource, static_cast<uint16_t>(source), 0, 0, 0});
}
EMSCRIPTEN_KEEPALIVE void midi(int source, int status, int first, int second) {
    ocarina::dispatchMidi(static_cast<uint16_t>(source), status, first, second, [](auto event) { voice.apply(event); });
}
EMSCRIPTEN_KEEPALIVE int32_t* sequenceData() { return sequencePacket.data(); }
EMSCRIPTEN_KEEPALIVE void commitSequence(int size) {
    if (size >= 0 && size <= static_cast<int>(sequencePacket.size()))
        voice.setSequence(std::span(sequencePacket.data(), static_cast<size_t>(size)));
}
EMSCRIPTEN_KEEPALIVE int sequencePosition() { return voice.sequencePosition(); }
EMSCRIPTEN_KEEPALIVE int sequenceActivity() { return voice.sequenceActivity(); }
EMSCRIPTEN_KEEPALIVE uint32_t heldNotes(int index) { return voice.heldNotes()[static_cast<size_t>(index) & 3]; }

EMSCRIPTEN_KEEPALIVE void parameter(int index, float value) { voice.setParameter(static_cast<size_t>(index), value); }
EMSCRIPTEN_KEEPALIVE int activeVoices() { return voice.soundingVoices(); }
EMSCRIPTEN_KEEPALIVE int latestNote() { return voice.latestNote(); }

EMSCRIPTEN_KEEPALIVE void performance(int model, int voices) {
    voice.setParameter(ocarina::model, static_cast<float>(model));
    voice.setParameter(ocarina::voiceLimit, static_cast<float>(voices));
    voice.setParameter(ocarina::voiceMode, voices == 0 ? 0.0f : 1.0f);
}

EMSCRIPTEN_KEEPALIVE float* render(int count) {
    voice.beginBlock();
    for (int index = 0; index < count && index < capacity; ++index) {
        const auto frame = voice.nextFrame();
        samples[index] = frame.left;
        samples[index + capacity] = frame.right;
    }
    return samples;
}
}
