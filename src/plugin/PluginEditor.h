#pragma once

#include "shared/Event.h"
#include "shared/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <mutex>

struct InstrumentMeter {
    std::atomic<float> peak {0};
    std::atomic<int> voices {0}, note {-1};
};

struct PatchRevision {
    std::recursive_mutex writer;
    std::atomic<unsigned> sequence {0};
    juce::var presetIdentity;

    class Write {
    public:
        explicit Write(PatchRevision& revision) : state(revision), lock(state.writer) {
            if (state.depth++ == 0) state.sequence.fetch_add(1);
        }
        ~Write() { if (--state.depth == 0) state.sequence.fetch_add(1); }
    private:
        PatchRevision& state;
        std::lock_guard<std::recursive_mutex> lock;
    };

private:
    unsigned depth = 0;
};

class InstrumentEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    InstrumentEditor(juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state,
        ocarina::EventQueue& keyboard, const InstrumentMeter& meter, PatchRevision& revision);
    ~InstrumentEditor() override;
    void resized() override;
    static bool available();
    static constexpr uint16_t keyboardSource = 2;

private:
    static std::optional<juce::WebBrowserComponent::Resource> resource(const juce::String& path);
    juce::WebBrowserComponent::Options options();
    void receive(const juce::var& event);
    void timerCallback() override;
    juce::File presetDirectory() const;
    void sendLibrary();
    void sendFileMessage(juce::Identifier key, const juce::var& value);
    void choosePresetFile(bool exportFile, const juce::String& name, const juce::String& json);

    ocarina::EventQueue& keyboard;
    const InstrumentMeter& meter;
    PatchRevision& revision;
    std::array<juce::RangedAudioParameter*, ocarina::parameterCount> parameters {};
    std::array<bool, ocarina::parameterCount> changing {};
    juce::WebBrowserComponent browser;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::var displayedPreset;
    bool ready = false;
};
