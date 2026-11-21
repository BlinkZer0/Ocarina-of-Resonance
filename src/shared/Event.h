#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace ocarina {

enum class EventType : uint8_t {
    noteOn, noteOff, releaseSource, releaseChannel, sustain, breath, expression,
    pitchBend, channelPressure, notePressure, parameter
};

struct Event {
    EventType type;
    uint16_t source;
    uint8_t channel;  // Zero-based MIDI channel.
    uint8_t note;    // MIDI note number, or parameter index for a parameter event.
    float value;    // Normalized to [0, 1], bipolar for pitch bend, native units for a parameter.
    uint32_t frame = 0;
};

// One control producer and one audio consumer. A dropped release triggers a reset
// at the start of the next drain so voice ownership cannot outlive the input.
class EventQueue {
public:
    bool pushBatch(std::span<const Event> batch) noexcept {
        if (overflow.load(std::memory_order_acquire)) return false;
        auto index = write.load(std::memory_order_relaxed);
        const auto consumed = read.load(std::memory_order_acquire);
        if (batch.size() > (consumed + capacity - index - 1) % capacity) return false;
        for (const auto& event : batch) {
            events[index] = event;
            index = (index + 1) % capacity;
        }
        write.store(index, std::memory_order_release);
        return true;
    }

    void push(Event event) noexcept {
        if (overflow.load(std::memory_order_acquire))
            return;
        const auto index = write.load(std::memory_order_relaxed);
        const auto next = (index + 1) % capacity;
        if (next == read.load(std::memory_order_acquire)) {
            overflow.store(true, std::memory_order_release);
            return;
        }
        events[index] = event;
        write.store(next, std::memory_order_release);
    }

    template<class Handle, class Reset>
    void drain(Handle&& handle, Reset&& reset) noexcept {
        auto index = read.load(std::memory_order_relaxed);
        const auto end = write.load(std::memory_order_acquire);
        if (overflow.load(std::memory_order_acquire)) {
            read.store(write.load(std::memory_order_acquire), std::memory_order_release);
            overflow.store(false, std::memory_order_release);
            std::forward<Reset>(reset)();
            return;
        }
        while (index != end) {
            handle(events[index]);
            index = (index + 1) % capacity;
        }
        read.store(index, std::memory_order_release);
    }

private:
    static constexpr size_t capacity = 256;
    Event events[capacity] {};
    std::atomic<size_t> read {0}, write {0};
    std::atomic<bool> overflow {false};
};

} // namespace ocarina
