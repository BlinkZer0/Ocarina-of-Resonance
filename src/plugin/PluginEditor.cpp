#include "PluginEditor.h"
#include "OcarinaPanel.h"

#include <WebView2.h>

#include <cmath>
#include <cstring>

using Browser = juce::WebBrowserComponent;

namespace {
constexpr int maxPresetBytes = 128 * 1024;
bool validPresetId(const juce::String& id) {
    return id.isNotEmpty() && id.length() <= 96
        && id.containsOnly("abcdefghijklmnopqrstuvwxyz0123456789-") && id[0] != '-';
}
}

bool PatchRevision::updateSequence(ocarina::InstrumentEngine& engine, const juce::var& value, bool command, bool restore) {
    auto* array = value.getArray();
    if (!array || array->size() < 7 || array->size() > static_cast<int>(ocarina::Sequencer::packetSize)) return false;
    std::array<int32_t, ocarina::Sequencer::packetSize> packet;
    for (int index = 0; index < array->size(); ++index) {
        const auto& entry = (*array)[index];
        const double number = entry;
        if (!(entry.isInt() || entry.isInt64() || entry.isDouble()) || !(number >= 0 && number <= INT32_MAX)
            || std::floor(number) != number) return false;
        packet[index] = static_cast<int32_t>(number);
    }
    const int next = sequenceCommand == INT32_MAX ? 1 : sequenceCommand + 1;
    packet[4] = command ? next : sequenceCommand;
    if (restore) packet[5] = packet[6] = 0;
    if (!engine.setSequence(std::span(packet.data(), static_cast<size_t>(array->size())))) return false;
    if (command) sequenceCommand = next;
    juce::Array<juce::var> saved;
    for (int index = 0; index < array->size(); ++index)
        saved.add(index >= 4 && index <= 6 ? 0 : packet[index]);
    sequencePacket = juce::var(saved);
    return true;
}

bool PatchRevision::updateMapping(const juce::var& value) {
    const auto* pairs = value.getArray();
    if (!pairs || pairs->size() % 2 || pairs->size() > static_cast<int>(2 * ccMap.size())) return false;
    std::array<int, 120> entries {};
    for (int index = 0; index < pairs->size(); index += 2) {
        const int cc = (*pairs)[index], parameter = (*pairs)[index + 1];
        if (cc < 0 || cc >= static_cast<int>(entries.size()) || parameter < 0 || parameter >= static_cast<int>(ocarina::parameterCount))
            return false;
        entries[static_cast<size_t>(cc)] = parameter + 1;
    }
    mapping = value;
    for (size_t index = 0; index < entries.size(); ++index)
        ccMap[index].store(entries[index], std::memory_order_relaxed);
    return true;
}

InstrumentEditor::InstrumentEditor(juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state,
    ocarina::EventQueue& keyboardToUse, const InstrumentMeter& meterToUse, PatchRevision& revisionToUse, ocarina::InstrumentEngine& engineToUse)
    : AudioProcessorEditor(processor), keyboard(keyboardToUse), meter(meterToUse), revision(revisionToUse), engine(engineToUse), browser(options()) {
    for (size_t index = 0; index < parameters.size(); ++index)
        parameters[index] = state.getParameter(ocarina::parameters[index].id.data());
    addAndMakeVisible(browser);
    setResizable(true, true);
    setResizeLimits(540, 390, 1600, 1120);
    setSize(1090, 715);
    browser.goToURL(Browser::getResourceProviderRoot() + "?v=" + JucePlugin_VersionString);
    startTimerHz(20);
}

InstrumentEditor::~InstrumentEditor() {
    stopTimer();
    for (size_t index = 0; index < changing.size(); ++index) {
        if (changing[index])
            parameters[index]->endChangeGesture();
    }
    keyboard.push({ocarina::EventType::releaseSource, keyboardSource, 0, 0, 0.0f});
}

void InstrumentEditor::resized() { browser.setBounds(getLocalBounds()); }

bool InstrumentEditor::available() {
    LPWSTR version = nullptr;
    const bool available = SUCCEEDED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &version)) && version != nullptr;
    CoTaskMemFree(version);
    return available;
}

Browser::Options InstrumentEditor::options() {
    using Options = Browser::Options;
    const auto userData = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("OcarinaOfResonance-WebView2");
    return Options{}.withBackend(Options::Backend::webview2)
        .withWinWebView2Options(Options::WinWebView2{}.withUserDataFolder(userData)
            .withBackgroundColour(juce::Colour(0xff0a101b)).withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider(resource)
        .withEventListener("instrumentInput", [this](const juce::var& event) { receive(event); });
}

std::optional<Browser::Resource> InstrumentEditor::resource(const juce::String& request) {
    const auto path = request.upToFirstOccurrenceOf("?", false, false);
    struct Route { const char* path; const char* data; int size; const char* mime; };
    using namespace OcarinaPanel;
    const Route routes[] {
        {"/", index_html, index_htmlSize, "text/html"}, {"/index.html", index_html, index_htmlSize, "text/html"},
        {"/assets/main.js", main_js, main_jsSize, "text/javascript"}, {"/assets/presets.js", presets_js, presets_jsSize, "text/javascript"},
        {"/assets/style.css", style_css, style_cssSize, "text/css"}, {"/assets/parameters.json", parameters_json, parameters_jsonSize, "application/json"},
        {"/assets/ocarina.webp", ocarina_webp, ocarina_webpSize, "image/webp"}, {"/assets/sequence.js", sequence_js, sequence_jsSize, "text/javascript"},
        {"/assets/midi-file.js", midifile_js, midifile_jsSize, "text/javascript"}
    };
    for (const auto& route : routes) {
        if (path != route.path)
            continue;
        std::vector<std::byte> buffer(static_cast<size_t>(route.size));
        std::memcpy(buffer.data(), route.data, buffer.size());
        return Browser::Resource {std::move(buffer), route.mime};
    }
    return std::nullopt;
}

void InstrumentEditor::receive(const juce::var& event) {
    const auto type = event.getProperty("type", {}).toString();
    if (type == "ready") {
        ready = true;
        timerCallback();
        sendLibrary();
        return;
    }
    if (type == "identifyPreset") {
        const std::scoped_lock lock(revision.writer);
        revision.presetIdentity = event.getProperty("preset", {});
        displayedPreset = revision.presetIdentity;
        return;
    }
    if (type == "listPresets") { sendLibrary(); return; }
    if (type == "importPreset") { chooseFile(false, {}, {}); return; }
    if (type == "importMidi") { chooseFile(false, {}, {}, true); return; }
    if (type == "exportMidi") {
        const auto data = event.getProperty("data", {}).toString();
        if (data.isNotEmpty() && data.length() <= 128 * 1024)
            chooseFile(true, "Ocarina sequence", data, true);
        return;
    }
    if (type == "sequence") {
        const auto value = event.getProperty("packet", {});
        const auto* packet = value.getArray();
        if (!packet || packet->size() < 7) return;
        const int command = (*packet)[4];
        const std::scoped_lock lock(revision.writer);
        if (revision.updateSequence(engine, value, command != lastSequenceCommand, false)) {
            lastSequenceCommand = command;
            displayedSequence = revision.sequencePacket;
        }
        return;
    }
    if (type == "midiMap") {
        const std::scoped_lock lock(revision.writer);
        if (revision.updateMapping(event.getProperty("pairs", {}))) displayedMapping = revision.mapping;
        return;
    }
    if (type == "savePreset" || type == "exportPreset") {
        const auto id = event.getProperty("id", {}).toString();
        const auto json = event.getProperty("json", {}).toString();
        if (!validPresetId(id) || json.isEmpty() || json.getNumBytesAsUTF8() > maxPresetBytes) {
            sendFileMessage("fileStatus", "Invalid preset data.");
            return;
        }
        if (type == "exportPreset") {
            chooseFile(true, event.getProperty("name", "Ocarina of Resonance").toString(), json);
        } else {
            const auto dir = presetDirectory();
            if (!dir.createDirectory() || !dir.getChildFile("patch-" + id + ".orpreset").replaceWithText(json)) {
                sendFileMessage("fileStatus", "Could not save in the Ocarina preset folder.");
            } else {
                sendLibrary();
            }
        }
        return;
    }
    if (type == "release") {
        keyboard.push({ocarina::EventType::releaseSource, keyboardSource, 0, 0, 0.0f});
        return;
    }
    if (type == "sustain") {
        keyboard.push({ocarina::EventType::sustain, keyboardSource, 0, 0, event.getProperty("on", false) ? 1.0f : 0.0f});
        return;
    }
    if (type == "patch") {
        const auto packet = event.getProperty("values", {});
        const auto* array = packet.getArray();
        if (!array || array->size() != static_cast<int>(parameters.size())) return;
        std::array<float, ocarina::parameterCount> snapshot;
        for (size_t index = 0; index < snapshot.size(); ++index) {
            const auto& value = (*array)[static_cast<int>(index)];
            if (!(value.isDouble() || value.isInt()) || !std::isfinite(static_cast<float>(value))) return;
            const auto& spec = ocarina::parameters[index];
            snapshot[index] = juce::jlimit(spec.minimum, spec.maximum, static_cast<float>(value));
        }
        const PatchRevision::Write write(revision);
        revision.presetIdentity = event.getProperty("preset", {});
        displayedPreset = revision.presetIdentity;
        for (size_t index = 0; index < snapshot.size(); ++index) {
            const float value = parameters[index]->convertTo0to1(snapshot[index]);
            if (parameters[index]->getValue() != value) parameters[index]->setValueNotifyingHost(value);
        }
        return;
    }
    if (type == "note") {
        const int key = event.getProperty("key", -1);
        const bool down = event.getProperty("on", false);
        if (key >= 0 && key <= 127)
            keyboard.push({down ? ocarina::EventType::noteOn : ocarina::EventType::noteOff,
                keyboardSource, 0, static_cast<uint8_t>(key), juce::jlimit(0.01f, 1.0f, static_cast<float>(event.getProperty("velocity", 0.8)))});
        return;
    }
    const int index = event.getProperty("index", -1);
    if (index < 0 || static_cast<size_t>(index) >= parameters.size())
        return;
    auto& parameter = *parameters[index];
    if (type == "begin" && !changing[index]) {
        changing[index] = true;
        parameter.beginChangeGesture();
    } else if (type == "end" && changing[index]) {
        changing[index] = false;
        parameter.endChangeGesture();
    } else if (type == "parameter") {
        const auto& control = ocarina::parameters[index];
        const float value = event.getProperty("value", control.initial);
        if (std::isfinite(value))
            parameter.setValueNotifyingHost(parameter.convertTo0to1(juce::jlimit(control.minimum, control.maximum, value)));
    }
}

juce::File InstrumentEditor::presetDirectory() const {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ocarina of Resonance").getChildFile("Presets");
}

void InstrumentEditor::sendFileMessage(juce::Identifier key, const juce::var& value) {
    auto result = std::make_unique<juce::DynamicObject>();
    result->setProperty(key, value);
    browser.emitEventIfBrowserIsVisible("instrumentState", juce::var(result.release()));
}

void InstrumentEditor::sendLibrary() {
    juce::Array<juce::var> files;
    for (const auto& file : presetDirectory().findChildFiles(juce::File::findFiles, false, "*.orpreset")) {
        if (file.getSize() > 0 && file.getSize() <= maxPresetBytes)
            files.add(file.loadFileAsString());
    }
    sendFileMessage("library", juce::var(files));
}

void InstrumentEditor::chooseFile(bool exportFile, const juce::String& name, const juce::String& data, bool midi) {
    if (chooser) return;
    const auto documents = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    const auto initial = exportFile ? documents.getChildFile(juce::File::createLegalFileName(name) + (midi ? ".mid" : ".orpreset")) : documents;
    const auto title = juce::String(exportFile ? "Export " : "Import ") + (midi ? "MIDI sequence" : "Ocarina preset");
    chooser = std::make_unique<juce::FileChooser>(title, initial, midi ? "*.mid;*.midi" : "*.orpreset;*.json");
    const int flags = juce::FileBrowserComponent::canSelectFiles
        | (exportFile ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting : juce::FileBrowserComponent::openMode);
    juce::Component::SafePointer<InstrumentEditor> self(this);
    chooser->launchAsync(flags, [self, exportFile, data, midi](const juce::FileChooser& completed) {
        if (!self) return;
        const auto file = completed.getResult();
        if (file != juce::File()) {
            if (midi) {
                juce::MemoryBlock bytes;
                juce::MemoryOutputStream decoded(bytes, false);
                if (exportFile) {
                    const bool written = juce::Base64::convertFromBase64(decoded, data) && file.replaceWithData(bytes.getData(), bytes.getSize());
                    self->sendFileMessage("midiNotice", written ? "MIDI exported." : "Could not write MIDI.");
                } else if (file.getSize() > 0 && file.getSize() <= maxPresetBytes && file.loadFileAsData(bytes)) {
                    self->sendFileMessage("midiFile", juce::Base64::toBase64(bytes.getData(), bytes.getSize()));
                } else {
                    self->sendFileMessage("midiNotice", "MIDI files must be 128 KB or smaller.");
                }
            } else if (exportFile) {
                self->sendFileMessage("fileStatus", file.replaceWithText(data) ? "Preset exported." : "Could not write the preset file.");
            } else if (file.getSize() > 0 && file.getSize() <= maxPresetBytes) {
                self->sendFileMessage("presetFile", file.loadFileAsString());
            } else {
                self->sendFileMessage("fileStatus", "Preset files must be 128 KB or smaller.");
            }
        }
        juce::MessageManager::callAsync([self] { if (self) self->chooser.reset(); });
    });
}

void InstrumentEditor::timerCallback() {
    if (!ready || !isShowing())
        return;
    auto result = std::make_unique<juce::DynamicObject>();
    juce::Array<juce::var> values;
    for (size_t index = 0; index < parameters.size(); ++index) {
        const auto* parameter = parameters[index];
        const float mapped = meter.mapped[index].load(std::memory_order_relaxed);
        values.add(std::isfinite(mapped) ? mapped : parameter->convertFrom0to1(parameter->getValue()));
    }
    result->setProperty("params", juce::var(values));
    result->setProperty("peak", meter.peak.load(std::memory_order_relaxed));
    result->setProperty("voices", meter.voices.load(std::memory_order_relaxed));
    result->setProperty("note", meter.note.load(std::memory_order_relaxed));
    result->setProperty("status", "VST3 audio ready");
    result->setProperty("tick", meter.tick.load(std::memory_order_relaxed));
    result->setProperty("transport", meter.transport.load(std::memory_order_relaxed));
    juce::Array<juce::var> held;
    for (const auto& word : meter.held) held.add(static_cast<juce::int64>(word.load(std::memory_order_relaxed)));
    result->setProperty("held", juce::var(held));
    const auto cc = meter.cc.load(std::memory_order_relaxed);
    if (cc != lastCC) {
        lastCC = cc;
        result->setProperty("midiCC", juce::var(juce::Array<juce::var>{int((cc >> 7) & 127), int(cc & 127)}));
    }
    {
        const std::scoped_lock lock(revision.writer);
        if (revision.presetIdentity != displayedPreset) {
            displayedPreset = revision.presetIdentity;
            result->setProperty("preset", revision.presetIdentity);
        }
        if (!revision.sequencePacket.equals(displayedSequence)) {
            displayedSequence = revision.sequencePacket;
            result->setProperty("sequence", displayedSequence);
        }
        if (!revision.mapping.equals(displayedMapping)) {
            displayedMapping = revision.mapping;
            result->setProperty("midiMap", displayedMapping);
        }
    }
    browser.emitEventIfBrowserIsVisible("instrumentState", juce::var(result.release()));
}
