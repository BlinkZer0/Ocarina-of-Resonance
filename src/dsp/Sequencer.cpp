#include "Sequencer.h"
#include "VoiceManager.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace ocarina {
bool Sequencer::publish(std::span<const int32_t> packet) noexcept {
    if (packet.size() < headerSize || packet.size() > packetSize || (packet.size() - headerSize) % 4)
        return false;
    const int length = packet[0];
    if (length < 4 * ticksPerBeat || length > maxBars * 4 * ticksPerBeat || packet[1] < 4000 || packet[1] > 25000
        || packet[4] < 0 || packet[6] < 0 || packet[6] > length)
        return false;
    auto& pattern = patterns[back];
    pattern.length = length;
    pattern.bpm100 = packet[1];
    pattern.repeat = packet[2] != 0;
    pattern.sync = packet[3] != 0;
    pattern.command = packet[4];
    pattern.play = packet[5] != 0;
    pattern.seek = packet[6];
    size_t count = 0;
    for (size_t index = headerSize; index < packet.size(); index += 4) {
        const Note note {packet[index], packet[index + 1], packet[index + 2], packet[index + 3]};
        if (note.key < 0 || note.key > 127 || note.start < 0 || note.start >= note.end || note.end > length
            || note.velocity < 1 || note.velocity > 127)
            return false;
        pattern.notes[count++] = note;
    }
    std::sort(pattern.notes.begin(), pattern.notes.begin() + count,
        [](const Note& left, const Note& right) {
            return std::tie(left.key, left.start, left.end, left.velocity) < std::tie(right.key, right.start, right.end, right.velocity);
        });
    // One active gate per pitch. Adjacent notes still get distinct articulations.
    pattern.count = 0;
    for (size_t index = 0; index < count; ++index) {
        const auto note = pattern.notes[index];
        if (pattern.count) {
            auto& prior = pattern.notes[pattern.count - 1];
            if (note.key == prior.key && note.start < prior.end) {
                prior.end = std::max(prior.end, note.end);
                continue;
            }
        }
        pattern.notes[pattern.count++] = note;
    }
    for (size_t index = 0; index < pattern.count; ++index) {
        const auto note = pattern.notes[index];
        pattern.edges[2 * index] = {note.start, note.key, note.velocity};
        pattern.edges[2 * index + 1] = {note.end, note.key, 0};
    }
    std::sort(pattern.edges.begin(), pattern.edges.begin() + 2 * pattern.count,
        [](const Edge& left, const Edge& right) {
            return std::tie(left.tick, left.velocity, left.key) < std::tie(right.tick, right.velocity, right.key);
        });
    back = middle.exchange(back | dirty, std::memory_order_acq_rel) & ~dirty;
    return true;
}

void Sequencer::prepare(double sampleRate) noexcept {
    rate = sampleRate;
    reset();
}

void Sequencer::reset() noexcept {
    cursor = step = expectedHost = 0;
    running = following = exhausted = false;
    next = 0;
    lastCommand = patterns[front].command;
}

void Sequencer::send(VoiceManager& voices, int key, int velocity) noexcept {
    voices.apply({velocity > 0 ? EventType::noteOn : EventType::noteOff, source, 0, static_cast<uint8_t>(key), velocity / 127.0f});
}

void Sequencer::release(VoiceManager& voices) noexcept {
    voices.apply({EventType::releaseSource, source, 0, 0, 0});
}

void Sequencer::move(VoiceManager& voices, double tick) noexcept {
    release(voices);
    cursor = tick;
    const auto& pattern = patterns[front];
    const auto end = pattern.edges.begin() + 2 * pattern.count;
    next = std::lower_bound(pattern.edges.begin(), end, tick,
        [](const Edge& edge, double target) { return edge.tick < target; }) - pattern.edges.begin();
    if (activity() && !exhausted) {
        for (size_t index = 0; index < pattern.count; ++index) {
            const auto note = pattern.notes[index];
            if (note.start < tick && tick < note.end) send(voices, note.key, note.velocity);
        }
    }
}

void Sequencer::begin(VoiceManager& voices, HostClock host) noexcept {
    const bool changed = (middle.load(std::memory_order_acquire) & dirty) != 0;
    if (changed) front = middle.exchange(front, std::memory_order_acq_rel) & ~dirty;
    const auto& pattern = patterns[front];
    const bool command = pattern.command != lastCommand;
    if (command) {
        lastCommand = pattern.command;
        running = pattern.play;
        cursor = pattern.seek;
        exhausted = false;
    }
    const bool hostActive = pattern.sync && host.available && host.playing && std::isfinite(host.quarter)
        && std::isfinite(host.bpm) && host.bpm > 0 && host.bpm <= 1000;
    if (hostActive) {
        running = false;
        const auto tick = host.quarter * ticksPerBeat;
        const auto bpmStep = host.bpm * ticksPerBeat / (60 * rate);
        if (!following || changed || std::abs(tick - expectedHost) > std::max(bpmStep * 2, 0.01)) {
            following = true;
            exhausted = tick < 0 || (!pattern.repeat && tick >= pattern.length);
            move(voices, exhausted ? std::clamp(tick, 0.0, static_cast<double>(pattern.length))
                : std::fmod(tick, pattern.length));
        }
        step = bpmStep;
        expectedHost = tick;
        return;
    }
    if (following) {
        following = false;
        if (!command) running = false;
        move(voices, std::clamp(cursor, 0.0, static_cast<double>(pattern.length)));
    } else if (changed || command) {
        exhausted = false;
        if (running && cursor >= pattern.length) cursor = 0;
        move(voices, std::min(cursor, static_cast<double>(pattern.length)));
    }
    step = pattern.bpm100 * ticksPerBeat / (6000 * rate);
}

void Sequencer::advance(VoiceManager& voices) noexcept {
    if (!activity()) return;
    const auto& pattern = patterns[front];
    expectedHost += step;
    if (exhausted) {
        if (following && expectedHost >= 0 && cursor == 0) {
            exhausted = false;
            move(voices, 0);
        }
        return;
    }
    if (cursor >= pattern.length - 1e-7) {
        if (pattern.repeat) move(voices, std::max(0.0, cursor - pattern.length));
        else {
            release(voices);
            running = false;
            exhausted = true;
            cursor = pattern.length;
            return;
        }
    }
    while (next < 2 * pattern.count && pattern.edges[next].tick <= cursor + 1e-7) {
        const auto edge = pattern.edges[next++];
        send(voices, edge.key, edge.velocity);
    }
    cursor += step;
}
} // namespace ocarina
