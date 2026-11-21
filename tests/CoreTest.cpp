#include "dsp/PrototypeVoice.h"

#include <cmath>
#include <iostream>

int main() {
    using ocarina::EventType;
    using ocarina::PrototypeVoice;
    PrototypeVoice voice;
    for (const int rate : {44100, 48000, 88200, 96000}) {
        voice.prepare(rate);
        if (voice.nextSample() != 0.0f)
            return 1;
        voice.apply({EventType::noteOn, 1, 0, 69, 1.0f});
        voice.apply({EventType::noteOn, 2, 0, 72, 1.0f});
        voice.apply({EventType::releaseSource, 2, 0, 0, 0.0f});
        int cycles = 0;
        float previous = 0.0f;
        for (int i = 0; i < rate; ++i) {
            const float current = voice.nextSample();
            if (!std::isfinite(current) || std::abs(current) > 0.31f)
                return 2;
            if (previous <= 0 && current > 0)
                ++cycles;
            previous = current;
        }
        if (std::abs(cycles - 440) > 1) {
            std::cerr << "Expected 440 Hz at " << rate << " Hz, got " << cycles << '\n';
            return 3;
        }
        voice.apply({EventType::noteOff, 1, 0, 69, 0.0f});
        for (int i = 0; i < rate; ++i)
            voice.nextSample();
        if (voice.nextSample() != 0.0f)
            return 4;
    }
    ocarina::EventQueue queue;
    bool reset = false;
    int delivered = 0;
    const auto drain = [&] { queue.drain([&](ocarina::Event) { ++delivered; }, [&] { reset = true; }); };
    for (int i = 0; i < 300; ++i)
        queue.push({EventType::noteOn, 1, 0, 69, 1.0f});
    drain();
    if (!reset || delivered != 0)
        return 5;
    queue.push({EventType::noteOff, 1, 0, 69, 0.0f});
    drain();
    if (delivered != 1)
        return 6;
    return 0;
}
