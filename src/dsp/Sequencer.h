#pragma once

#include "shared/Event.h"

#include <array>

namespace ocarina {
class VoiceManager;

struct HostClock {
    double quarter = 0, bpm = 120;
    bool available = false, playing = false;
};

class Sequencer {
public:
    static constexpr int ticksPerBeat = 960, maxBars = 64, maxNotes = 512;
    static constexpr size_t headerSize = 7, packetSize = headerSize + maxNotes * 4;
    static constexpr uint16_t source = 3;

    // One control producer, one audio consumer. The three buffers retain ownership
    // even if several edits arrive during a single render block.
    bool publish(std::span<const int32_t> packet) noexcept;
    void prepare(double sampleRate) noexcept;
    void begin(VoiceManager& voices, HostClock host) noexcept;
    void advance(VoiceManager& voices) noexcept;
    void reset() noexcept;
    int position() const noexcept { return static_cast<int>(cursor); }
    int activity() const noexcept { return following ? 2 : running ? 1 : 0; }
    float localTempo() const noexcept {
        return running ? patterns[front].bpm100 * .01f : 0.0f;
    }

private:
    struct Note { int key = 0, start = 0, end = 0, velocity = 0; };
    struct Edge { int tick = 0, key = 0, velocity = 0; };
    struct Pattern {
        std::array<Note, maxNotes> notes;
        std::array<Edge, maxNotes * 2> edges;
        size_t count = 0;
        int length = 4 * ticksPerBeat * 2, bpm100 = 12000, command = 0, seek = 0;
        bool repeat = true, sync = false, play = false;
    };

    void move(VoiceManager& voices, double tick) noexcept;
    void release(VoiceManager& voices) noexcept;
    void send(VoiceManager& voices, int key, int velocity) noexcept;

    std::array<Pattern, 3> patterns {};
    std::atomic<unsigned> middle {1};
    unsigned front = 0, back = 2;  // Owned by audio and control threads, respectively.
    static constexpr unsigned dirty = 4;
    size_t next = 0;
    double cursor = 0, step = 0, rate = 48000, expectedHost = 0;
    int lastCommand = 0;
    bool following = false, running = false, exhausted = false;
};
} // namespace ocarina
