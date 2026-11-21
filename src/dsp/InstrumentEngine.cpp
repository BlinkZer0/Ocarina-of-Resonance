#include "InstrumentEngine.h"

#include <algorithm>
#include <cmath>

namespace ocarina {

void InstrumentEngine::prepare(double rate) {
    rate = std::isfinite(rate) ? std::clamp(rate, 8000.0, 384000.0) : 48000.0;
    voices.setParameter(master, 100);
    voices.prepare(rate);
    effects.prepare(static_cast<float>(rate));
    sequencer.prepare(rate);
}

void InstrumentEngine::reset() noexcept {
    voices.reset();
    effects.reset();
    sequencer.reset();
}

void InstrumentEngine::setParameter(size_t index, float value) noexcept {
    if (index != master && index < synthesisParameterCount) voices.setParameter(index, value);
    else effects.setParameter(index, value);
}

void InstrumentEngine::apply(Event event) noexcept {
    if (event.type == EventType::parameter) setParameter(event.note, event.value);
    else voices.apply(event);
}

} // namespace ocarina
