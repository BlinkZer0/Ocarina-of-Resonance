#pragma once

#include "EffectsEngine.h"
#include "VoiceManager.h"

namespace ocarina {

class InstrumentEngine {
public:
    static constexpr int maxVoices = VoiceManager::maxVoices;
    void prepare(double rate);
    void reset() noexcept;
    void releaseAll() noexcept { voices.releaseAll(); }
    void setParameter(size_t index, float value) noexcept;
    void setHostTempo(float bpm) noexcept { effects.setHostTempo(bpm); }
    void apply(Event event) noexcept;
    StereoFrame nextFrame() noexcept { return effects.process(voices.nextSample(), voices.latestNote()); }
    int soundingVoices() const noexcept { return voices.soundingVoices(); }
    int latestNote() const noexcept { return voices.latestNote(); }

private:
    VoiceManager voices;
    EffectsEngine effects;
};

} // namespace ocarina
