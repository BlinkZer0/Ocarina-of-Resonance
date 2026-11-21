#pragma once

#include "Event.h"

namespace ocarina {
template<class Sink>
void dispatchMidi(uint16_t source, int status, int first, int second, Sink&& sink, uint32_t frame = 0) noexcept {
    if (status < 0x80 || status >= 0xf0 || first < 0 || first > 127 || second < 0 || second > 127) return;
    using enum EventType;
    const auto send = [&](EventType type, float value = 0, int note = 0) {
        sink(Event {type, source, static_cast<uint8_t>(status & 15), static_cast<uint8_t>(note), value, frame});
    };
    const auto value = second / 127.0f;
    switch (status & 0xf0) {
        case 0x80: send(noteOff, value, first); break;
        case 0x90: send(second ? noteOn : noteOff, value, first); break;
        case 0xa0: send(notePressure, value, first); break;
        case 0xd0: send(channelPressure, first / 127.0f); break;
        case 0xe0: {
            const int bend = second * 128 + first - 8192;
            send(pitchBend, bend / (bend < 0 ? 8192.0f : 8191.0f));
            break;
        }
        case 0xb0:
            switch (first) {
                case 1: send(modulation, value); break;
                case 2: send(breath, value); break;
                case 11: send(expression, value); break;
                case 64: send(sustain, value); break;
                case 120: case 123: send(releaseChannel); break;
                case 121: send(sustain); send(breath, 1); send(expression, 1); send(pitchBend); send(channelPressure); send(modulation); break;
                default: break;
            }
            break;
        default: break;
    }
}
} // namespace ocarina
