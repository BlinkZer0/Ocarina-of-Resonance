#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace ocarina {

enum class EventType : uint8_t { noteOn, noteOff, releaseSource };

struct Event {
    EventType type;
    uint16_t source;
    uint8_t channel;  // Zero-based MIDI channel.
    uint8_t note;     // MIDI note number.
    float velocity;  // Normalized to [0, 1].
    uint32_t frame = 0;
};

// One control producer and one audio consumer. A dropped release triggers a reset
// at the start of the next drain so voice ownership cannot outlive the input.
class EventQueue {
public:
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
