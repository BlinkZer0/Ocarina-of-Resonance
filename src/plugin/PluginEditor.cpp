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

InstrumentEditor::InstrumentEditor(juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state,
    ocarina::EventQueue& keyboardToUse, const InstrumentMeter& meterToUse, PatchRevision& revisionToUse)
    : AudioProcessorEditor(processor), keyboard(keyboardToUse), meter(meterToUse), revision(revisionToUse), browser(options()) {
    for (size_t index = 0; index < parameters.size(); ++index)
        parameters[index] = state.getParameter(ocarina::parameters[index].id.data());
    addAndMakeVisible(browser);
    setResizable(true, true);
    setResizeLimits(540, 390, 1600, 1120);
    setSize(1090, 715);
    browser.goToURL(Browser::getResourceProviderRoot());
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
    struct Route { const char* path; const char* name; const char* mime; };
    constexpr Route routes[] {
        {"/", "index_html", "text/html"}, {"/index.html", "index_html", "text/html"},
        {"/assets/main.js", "main_js", "text/javascript"}, {"/assets/presets.js", "presets_js", "text/javascript"}, {"/assets/style.css", "style_css", "text/css"},
        {"/assets/parameters.json", "parameters_json", "application/json"}, {"/assets/ocarina.webp", "ocarina_webp", "image/webp"}
    };
    for (const auto& route : routes) {
        if (path != route.path)
            continue;
        int size = 0;
        if (const auto* bytes = OcarinaPanel::getNamedResource(route.name, size)) {
            std::vector<std::byte> buffer(static_cast<size_t>(size));
            std::memcpy(buffer.data(), bytes, buffer.size());
            return Browser::Resource {std::move(buffer), route.mime};
        }
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
    if (type == "importPreset") { choosePresetFile(false, {}, {}); return; }
    if (type == "savePreset" || type == "exportPreset") {
        const auto id = event.getProperty("id", {}).toString();
        const auto json = event.getProperty("json", {}).toString();
        if (!validPresetId(id) || json.isEmpty() || json.getNumBytesAsUTF8() > maxPresetBytes) {
            sendFileMessage("fileStatus", "Invalid preset data.");
            return;
        }
        if (type == "exportPreset") {
            choosePresetFile(true, event.getProperty("name", "Ocarina of Resonance").toString(), json);
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
                keyboardSource, 0, static_cast<uint8_t>(key), 0.8f});
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

void InstrumentEditor::choosePresetFile(bool exportFile, const juce::String& name, const juce::String& json) {
    if (chooser) return;
    const auto documents = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    const auto initial = exportFile ? documents.getChildFile(juce::File::createLegalFileName(name) + ".orpreset") : documents;
    chooser = std::make_unique<juce::FileChooser>(exportFile ? "Export Ocarina preset" : "Import Ocarina preset",
        initial, "*.orpreset;*.json");
    const int flags = juce::FileBrowserComponent::canSelectFiles
        | (exportFile ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting : juce::FileBrowserComponent::openMode);
    juce::Component::SafePointer<InstrumentEditor> self(this);
    chooser->launchAsync(flags, [self, exportFile, json](const juce::FileChooser& completed) {
        if (!self) return;
        const auto file = completed.getResult();
        if (file != juce::File()) {
            if (exportFile) {
                self->sendFileMessage("fileStatus", file.replaceWithText(json) ? "Preset exported." : "Could not write the preset file.");
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
    for (const auto* parameter : parameters)
        values.add(parameter->convertFrom0to1(parameter->getValue()));
    result->setProperty("params", juce::var(values));
    result->setProperty("peak", meter.peak.load(std::memory_order_relaxed));
    result->setProperty("voices", meter.voices.load(std::memory_order_relaxed));
    result->setProperty("note", meter.note.load(std::memory_order_relaxed));
    result->setProperty("status", "VST3 audio ready");
    {
        const std::scoped_lock lock(revision.writer);
        if (revision.presetIdentity != displayedPreset) {
            displayedPreset = revision.presetIdentity;
            result->setProperty("preset", revision.presetIdentity);
        }
    }
    browser.emitEventIfBrowserIsVisible("instrumentState", juce::var(result.release()));
}
