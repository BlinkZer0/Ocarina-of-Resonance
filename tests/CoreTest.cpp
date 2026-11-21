#include "dsp/VoiceManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using ocarina::EventType;
using ocarina::VoiceManager;

void require(bool condition, const char* explanation) {
    if (!condition)
        throw std::runtime_error(explanation);
}

void note(VoiceManager& engine, int key, float velocity = 1, int source = 1, int channel = 0) {
    engine.apply({velocity > 0 ? EventType::noteOn : EventType::noteOff, static_cast<uint16_t>(source),
        static_cast<uint8_t>(channel), static_cast<uint8_t>(key), velocity});
}

void advance(VoiceManager& engine, int frames) {
    for (int i = 0; i < frames; ++i) {
        const float sample = engine.nextSample();
        require(std::isfinite(sample) && std::abs(sample) < 1.0f, "output is finite and below full scale");
    }
}

std::vector<float> samples(VoiceManager& engine, int frames) {
    std::vector<float> result(frames);
    std::generate(result.begin(), result.end(), [&] { return engine.nextSample(); });
    return result;
}

double rms(std::span<const float> signal) {
    double energy = 0;
    for (const float sample : signal)
        energy += sample * sample;
    return std::sqrt(energy / signal.size());
}

double pitch(std::span<const float> signal, int rate) {
    float previous = 0;
    bool armed = false;
    double first = -1, last = -1;
    int periods = 0;
    for (size_t index = 0; index < signal.size(); ++index) {
        const float sample = signal[index];
        armed = armed || sample < -0.025f;
        if (armed && previous <= 0 && sample > 0) {
            const double crossing = static_cast<double>(index) - sample / (sample - previous);
            if (first >= 0)
                ++periods;
            else
                first = crossing;
            last = crossing;
            armed = false;
        }
        previous = sample;
    }
    return periods > 0 ? periods * rate / (last - first) : 0;
}

double harmonic(std::span<const float> signal, int rate, double frequency) {
    double real = 0, imaginary = 0;
    const double turn = 2 * std::numbers::pi * frequency / rate;
    for (size_t index = 0; index < signal.size(); ++index) {
        const double window = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * index / signal.size());
        real += signal[index] * window * std::cos(index * turn);
        imaginary += signal[index] * window * std::sin(index * turn);
    }
    return std::hypot(real, imaginary);
}

void checkTuning() {
    constexpr std::array rates {44100, 48000, 88200, 96000};
    double worst = 0;
    for (const int rate : rates) {
        for (const int key : {36, 48, 60, 69, 84, 96}) {
            VoiceManager engine;
            engine.setParameter(ocarina::vibratoDepth, 0);
            engine.prepare(rate);
            require(engine.nextSample() == 0, "silence before a note");
            note(engine, key);
            advance(engine, rate / 3);
            const auto signal = samples(engine, rate / 2);
            const double expected = 440.0 * std::exp2((key - 69) / 12.0);
            const double measured = pitch(signal, rate);
            const double cents = measured > 0 ? std::abs(1200 * std::log2(measured / expected)) : std::numeric_limits<double>::infinity();
            worst = std::max(worst, cents);
            if (cents > 10.0)
                std::cerr << rate << " Hz, MIDI " << key << ": expected " << expected << ", measured " << measured << '\n';
            require(cents <= 10.0, "sustained tuning within 10 cents");
            note(engine, key, 0);
            advance(engine, rate / 2);
            require(engine.nextSample() == 0.0f, "tail settles to silence");
        }
    }
    std::cout << "Tuning: maximum " << worst << " cents across 24 note/rate pairs\n";
}

void checkModesAndArticulation() {
    constexpr int rate = 48000;
    std::array<double, 4> third {}, fourth {};
    for (size_t mode = 0; mode < ocarina::modelNames.size(); ++mode) {
        VoiceManager engine;
        engine.setParameter(ocarina::model, static_cast<float>(mode));
        engine.setParameter(ocarina::vibratoDepth, 0);
        engine.prepare(rate);
        note(engine, 69);
        const auto onset = samples(engine, rate / 10);
        require(rms(std::span(onset).first(rate / 200)) < 0.5 * rms(std::span(onset).last(rate / 50)), "attack builds smoothly");
        advance(engine, rate / 4);
        const auto signal = samples(engine, rate / 4);
        const auto frequency = pitch(signal, rate);
        require(frequency > 435 && frequency < 445, "all four modes retain the fundamental");
        const auto fundamental = harmonic(signal, rate, frequency);
        third[mode] = harmonic(signal, rate, frequency * 3) / fundamental;
        fourth[mode] = harmonic(signal, rate, frequency * 4) / fundamental;
        std::cout << ocarina::modelNames[mode] << ": H3/H1 " << third[mode] << ", H4/H1 " << fourth[mode] << '\n';
    }
    require(third[1] < third[0] && third[2] > 2 * third[0], "retro rolls off harmonics and crystal enhances them");
    require(fourth[3] > 2 * fourth[2], "hybrid emphasizes the fourth partial");

    VoiceManager engine;
    engine.prepare(rate);
    note(engine, 69, 0.25f);
    advance(engine, rate / 2);
    const auto soft = rms(samples(engine, rate / 10));
    engine.reset();
    note(engine, 69, 1.0f);
    advance(engine, rate / 2);
    const auto strong = rms(samples(engine, rate / 10));
    require(soft > 0.01 && strong > soft * 1.25, "velocity shapes the sustained drive");

    engine.reset();
    engine.apply({EventType::breath, 1, 0, 0, 0});
    note(engine, 69);
    advance(engine, rate / 2);
    require(engine.nextSample() == 0.0f, "closed breath control prevents drive");
    engine.apply({EventType::breath, 1, 0, 0, 1});
    advance(engine, rate / 2);
    require(rms(samples(engine, rate / 10)) > 0.06, "opening breath starts a held note");
    engine.apply({EventType::pitchBend, 1, 0, 0, 1});
    advance(engine, rate / 4);
    require(std::abs(pitch(samples(engine, rate / 4), rate) - 493.883) < 5, "pitch bend reaches its nominal range");
}

void checkVoiceOwnership() {
    constexpr int rate = 48000;
    VoiceManager engine;
    engine.setParameter(ocarina::voiceMode, 0);
    engine.prepare(rate);
    note(engine, 60);
    advance(engine, rate / 4);
    note(engine, 69, 1, 2);
    require(engine.soundingVoices() == 1, "mono legato keeps a single resonator");
    advance(engine, rate / 4);
    require(std::abs(pitch(samples(engine, rate / 4), rate) - 440) < 5, "legato reaches the new pitch");
    engine.apply({EventType::releaseSource, 2, 0, 0, 0});
    advance(engine, rate / 4);
    require(std::abs(pitch(samples(engine, rate / 4), rate) - 261.63) < 4, "mono returns to the previous source");

    engine.setParameter(ocarina::voiceMode, 1);
    engine.releaseAll();
    engine.reset();
    for (int key = 48; key < 64; ++key)
        note(engine, key);
    require(engine.soundingVoices() == 16, "all sixteen voice slots render");
    advance(engine, rate / 4);
    const auto beforeSteal = engine.nextSample();
    note(engine, 80);
    require(engine.soundingVoices() == 16, "voice stealing respects the limit");
    require(std::abs(engine.nextSample() - beforeSteal) < 0.18f, "voice stealing maintains sample continuity");
    engine.releaseAll();
    advance(engine, rate / 2);
    require(engine.soundingVoices() == 0 && engine.nextSample() == 0, "panic clears keys and tails");

    engine.setParameter(ocarina::voiceLimit, 2);
    engine.apply({EventType::sustain, 1, 0, 0, 1});
    note(engine, 69);
    note(engine, 72, 1, 1, 1);
    note(engine, 69, 0);
    advance(engine, rate / 2);
    require(engine.soundingVoices() == 2, "sustain holds the released key on its channel");
    engine.apply({EventType::sustain, 1, 0, 0, 0});
    advance(engine, rate / 2);
    require(engine.soundingVoices() == 1 && rms(samples(engine, rate / 10)) > 0.05, "pedal release preserves other channels");
}

void checkStress() {
    for (const int rate : {44100, 96000}) {
        VoiceManager engine;
        for (int value = 0; value < 2; ++value) {
            for (size_t index = 0; index < ocarina::parameters.size(); ++index) {
                if (index >= ocarina::model && index <= ocarina::voiceLimit)
                    continue;
                const auto& spec = ocarina::parameters[index];
                engine.setParameter(index, value ? spec.maximum : spec.minimum);
            }
            engine.prepare(rate);
            for (int mode = 0; mode < 4; ++mode) {
                engine.setParameter(ocarina::model, static_cast<float>(mode));
                for (int key = 0; key < 128; ++key) {
                    note(engine, key);
                    advance(engine, 128);
                    if (key >= 16)
                        note(engine, key - 16, 0);
                }
                engine.releaseAll();
                advance(engine, rate / 2);
                require(engine.nextSample() == 0, "stress case releases all notes");
            }
        }
    }

    ocarina::EventQueue queue;
    bool reset = false;
    int delivered = 0;
    const auto drain = [&] { queue.drain([&](ocarina::Event) { ++delivered; }, [&] { reset = true; }); };
    for (int index = 0; index < 300; ++index)
        queue.push({EventType::noteOn, 1, 0, 69, 1});
    drain();
    require(reset && delivered == 0, "queue overflow clears pending notes");
    queue.push({EventType::noteOff, 1, 0, 69, 0});
    drain();
    require(delivered == 1, "the queue accepts fresh events after overflow");
}

void benchmark() {
    constexpr int rate = 48000, seconds = 10;
    VoiceManager engine;
    engine.setParameter(ocarina::model, 3);
    engine.prepare(rate);
    for (int key = 48; key < 64; ++key)
        note(engine, key, 0.8f);
    advance(engine, rate / 4);
    const auto start = std::chrono::steady_clock::now();
    double checksum = 0;
    for (int frame = 0; frame < rate * seconds; ++frame)
        checksum += engine.nextSample();
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    require(std::isfinite(checksum), "benchmark output is finite");
    std::cout << "16 voices at 48 kHz, Electronic Hybrid: " << wall * 1000 << " ms for " << seconds
        << " s (" << wall * 100 / seconds << "% of one real-time thread)\n";
}

void render(const std::filesystem::path& directory) {
    constexpr int rate = 48000, frames = rate * 3;
    const std::array names {"ceramic-authentic.wav", "retro-fantasy.wav", "crystal-resonator.wav", "electronic-hybrid.wav"};
    std::filesystem::create_directories(directory);
    for (size_t mode = 0; mode < names.size(); ++mode) {
        VoiceManager engine;
        engine.setParameter(ocarina::model, static_cast<float>(mode));
        engine.setParameter(ocarina::voiceMode, 0);
        engine.prepare(rate);
        std::ofstream wav(directory / names[mode], std::ios::binary);
        require(wav.good(), "could not create render");
        const auto write = [&](uint32_t value, int count) {
            for (int byte = 0; byte < count; ++byte)
                wav.put(static_cast<char>((value >> (byte * 8)) & 0xff));
        };
        wav.write("RIFF", 4); write(36 + frames * 2, 4);
        wav.write("WAVEfmt ", 8); write(16, 4); write(1, 2); write(1, 2);
        write(rate, 4); write(rate * 2, 4); write(2, 2); write(16, 2);
        wav.write("data", 4); write(frames * 2, 4);
        for (int frame = 0; frame < frames; ++frame) {
            if (frame == rate / 10) note(engine, 65, 0.48f);
            if (frame == rate * 3 / 5) note(engine, 69, 0.85f);
            if (frame == rate * 7 / 10) note(engine, 65, 0);
            if (frame == rate * 6 / 5) note(engine, 69, 0);
            if (frame == rate * 7 / 5) note(engine, 72);
            if (frame == rate * 2) note(engine, 76, 0.7f);
            if (frame == rate * 21 / 10) note(engine, 72, 0);
            if (frame == rate * 5 / 2) note(engine, 76, 0);
            write(static_cast<uint16_t>(static_cast<int16_t>(std::lround(engine.nextSample() * 32767))), 2);
        }
        require(wav.good(), "could not write render");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--benchmark") {
            benchmark();
        } else if (argc == 3 && std::string_view(argv[1]) == "--render") {
            render(argv[2]);
        } else {
            checkTuning();
            checkModesAndArticulation();
            checkVoiceOwnership();
            checkStress();
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
