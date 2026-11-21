#pragma once

#include "EffectsEngine.h"
#include "Sequencer.h"
#include "VoiceManager.h"

namespace ocarina {

class InstrumentEngine {
public:
    static constexpr int maxVoices = VoiceManager::maxVoices;
    void prepare(double rate);
    void reset() noexcept;
    void releaseAll() noexcept { voices.releaseAll(); }
    void setParameter(size_t index, float value) noexcept;
    void apply(Event event) noexcept;
    bool setSequence(std::span<const int32_t> packet) noexcept { return sequencer.publish(packet); }
    void beginBlock(HostClock clock = {}, float hostTempo = 0) noexcept {
        sequencer.begin(voices, clock);
        const float localTempo = sequencer.localTempo();
        effects.setHostTempo(localTempo > 0 ? localTempo : hostTempo);
    }
    int sequencePosition() const noexcept { return sequencer.position(); }
    int sequenceActivity() const noexcept { return sequencer.activity(); }
    StereoFrame nextFrame() noexcept {
        sequencer.advance(voices);
        return effects.process(voices.nextSample(), voices.latestNote());
    }
    int soundingVoices() const noexcept { return voices.soundingVoices(); }
    int latestNote() const noexcept { return voices.latestNote(); }
    std::array<uint32_t, 4> heldNotes() const noexcept { return voices.heldNotes(); }

private:
    VoiceManager voices;
    EffectsEngine effects;
    Sequencer sequencer;
};

} // namespace ocarina
