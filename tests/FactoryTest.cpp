#include "dsp/InstrumentEngine.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {
using namespace ocarina;
constexpr int rate = 48000;

void verify(bool valid, const char* reason) {
    if (!valid) throw std::runtime_error(reason);
}

void verifyFrame(StereoFrame frame) {
    verify(std::isfinite(frame.left) && std::isfinite(frame.right), "Non-finite sample");
    verify(std::abs(frame.left) < 1.0f && std::abs(frame.right) < 1.0f, "Clipped output");
}

double fingerprint(EffectsEngine& processor) {
    double difference = 0;
    for (int frame = 0; frame < rate; ++frame) {
        const float input = .23f * std::sin(.045f * frame) + .061f * std::sin(.141f * frame);
        const auto output = processor.process(input, 60);
        verifyFrame(output);
        difference += std::abs(output.left - input) + std::abs(output.right - input);
    }
    return difference;
}

void verifyProcessors() {
    constexpr std::array switches {filterEnabled, driveEnabled, fmEnabled, grainEnabled, tremoloEnabled,
        phaserEnabled, chorusEnabled, crusherEnabled, reducerEnabled, delayEnabled, reverbEnabled};
    EffectsEngine dry;
    dry.setParameter(master, 100);
    dry.prepare(rate);
    verify(fingerprint(dry) < .001, "Disabled effects should pass the dry signal");
    const auto configure = [](EffectsEngine& engine) {
        engine.setParameter(master, 100);
        engine.setParameter(filterDb, 11);
        engine.setParameter(filterHz, 350);
        engine.setParameter(crusherBits, 4);
        engine.setParameter(reducerRate, 850);
        engine.setParameter(fmRatio, 2.2f);
        engine.setParameter(grainPitch, 7);
    };
    for (const auto enabled : switches) {
        EffectsEngine processor;
        configure(processor);
        processor.setParameter(enabled, 1);
        processor.prepare(rate);
        verify(fingerprint(processor) > 30, "An enabled processor did not change the signal");
    }

    EffectsEngine delay;
    delay.setParameter(master, 100);
    delay.setParameter(delayEnabled, 1);
    delay.setParameter(delayMode, 1);
    delay.setParameter(delaySync, 1);
    delay.setParameter(delayDivision, 1);
    delay.setParameter(delayMix, 100);
    delay.setParameter(delayFeedback, 45);
    delay.setHostTempo(120);
    delay.prepare(rate);
    for (int frame = 0; frame <= rate / 2; ++frame) {
        const auto output = delay.process(frame == 0 ? .5f : 0.0f, 60);
        if (frame < rate / 4) verify(output.left == 0 && output.right == 0, "Echo precedes its scheduled beat");
        if (frame == rate / 4) verify(output.left > .4f && output.right == 0, "First ping-pong tap is left");
        if (frame == rate / 2) verify(output.right > .01f && output.left == 0, "Second ping-pong tap answers right");
    }

    for (const int sampleRate : {44100, 48000, 96000}) {
        InstrumentEngine engine;
        for (size_t index = patchGain; index < parameterCount; ++index) engine.setParameter(index, parameters[index].maximum);
        engine.prepare(sampleRate);
        for (int note = 48; note < 64; ++note)
            engine.apply({EventType::noteOn, 1, 0, static_cast<uint8_t>(note), .8f});
        float peak = 0;
        for (int frame = 0; frame < sampleRate * 3; ++frame) {
            if (frame == sampleRate) engine.releaseAll();
            if (frame == 2 * sampleRate) engine.setParameter(reverbMode, 3);
            const auto sample = engine.nextFrame();
            peak = std::max({peak, std::abs(sample.left), std::abs(sample.right)});
            verifyFrame(sample);
        }
        verify(peak > .1f, "Stress patch never sounded");
        engine.reset();
        const auto silent = engine.nextFrame();
        verify(silent.left == 0 && silent.right == 0, "The effects reset left a stale tail");
    }
}

void renderFactory(const char* path, bool measure) {
    std::ifstream source(path);
    verify(source.good(), "Factory vectors missing");
    std::unordered_set<std::string> ids;
    InstrumentEngine engine;
    std::string row;
    while (std::getline(source, row)) {
        std::istringstream items(row);
        std::string id;
        verify(static_cast<bool>(items >> id) && ids.insert(id).second, "Invalid or duplicate factory preset ID");
        for (size_t index = 0; index < parameterCount; ++index) {
            float value;
            verify(static_cast<bool>(items >> value) && std::isfinite(value), "Incomplete factory snapshot");
            engine.setParameter(index, value);
        }
        std::string extra;
        verify(!(items >> extra), "Unexpected factory parameter");
        engine.prepare(rate);
        engine.apply({EventType::noteOn, 1, 0, 60, .8f});
        double power = 0, stereo = 0;
        float peak = 0;
        for (int index = 0; index < 2 * rate; ++index) {
            if (index == rate) engine.apply({EventType::noteOff, 1, 0, 60, 0});
            const auto frame = engine.nextFrame();
            verifyFrame(frame);
            if (index >= rate / 2 && index < rate) power += frame.left * frame.left + frame.right * frame.right;
            stereo += std::abs(frame.left - frame.right);
            peak = std::max({peak, std::abs(frame.left), std::abs(frame.right)});
        }
        const double db = 10.0 * std::log10(power / rate);
        std::cout << id << '\t' << std::fixed << std::setprecision(5) << db << '\t' << peak << '\t' << stereo << '\n';
        verify(db > -39 && peak < .96 && stereo > 0, "Factory preset did not produce a safe stereo render");
        verify(measure || std::abs(db + 21.0) <= .85, "Factory preset level exceeds the calibration tolerance");
    }
    verify(ids.size() == 40, "Exactly forty factory presets required");
}
} // namespace

int main(int argc, char** argv) {
    try {
        verify(argc == 2 || (argc == 3 && std::string(argv[2]) == "--measure"), "Expected factory vectors path");
        if (argc == 2) verifyProcessors();
        renderFactory(argv[1], argc == 3);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
