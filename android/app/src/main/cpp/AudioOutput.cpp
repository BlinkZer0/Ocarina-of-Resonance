#include "dsp/InstrumentEngine.h"
#include "shared/Midi.h"

#include <jni.h>
#include <oboe/Oboe.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>

namespace {

class Renderer final : public oboe::AudioStreamDataCallback, public oboe::AudioStreamErrorCallback {
public:
    void prepare(int rate) {
        voice.prepare(rate);
        fadeStep = 1.0f / std::max(1.0f, rate * .008f);
        gain = 0;
    }

    void note(bool on, int key, int source, float velocity) noexcept {
        if (key >= 0 && key <= 127 && source >= 0 && source <= 65535) {
            queue.push({on ? ocarina::EventType::noteOn : ocarina::EventType::noteOff,
                static_cast<uint16_t>(source), 0, static_cast<uint8_t>(key), velocity});
        }
    }

    void midi(int source, int status, int first, int second) noexcept {
        ocarina::dispatchMidi(static_cast<uint16_t>(source), status, first, second, [&](auto event) { queue.push(event); });
    }

    bool sequence(std::span<const int32_t> packet) noexcept { return voice.setSequence(packet); }

    void performance(int model, int voices) noexcept {
        using namespace ocarina;
        queue.push({EventType::parameter, 0, 0, ParameterIndex::model, static_cast<float>(model)});
        queue.push({EventType::parameter, 0, 0, voiceLimit, static_cast<float>(voices)});
        queue.push({EventType::parameter, 0, 0, voiceMode, voices == 0 ? 0.0f : 1.0f});
    }

    void parameter(int index, float value) noexcept {
        if (index >= 0 && static_cast<size_t>(index) < ocarina::parameterCount && std::isfinite(value))
            queue.push({ocarina::EventType::parameter, 0, 0, static_cast<uint8_t>(index), value});
    }

    void release(uint16_t source) noexcept {
        queue.push({ocarina::EventType::releaseSource, source, 0, 0, 0.0f});
    }

    bool patch(const std::array<float, ocarina::parameterCount>& values) noexcept {
        std::array<ocarina::Event, ocarina::parameterCount> events;
        for (size_t index = 0; index < values.size(); ++index) {
            if (!std::isfinite(values[index])) return false;
            events[index] = {ocarina::EventType::parameter, 0, 0, static_cast<uint8_t>(index), values[index]};
        }
        return queue.pushBatch(events);
    }

    void beginProfile() noexcept {
        for (auto& bucket : histogram)
            bucket.store(0, std::memory_order_relaxed);
        peak.store(0, std::memory_order_relaxed);
        profiling.store(true, std::memory_order_release);
    }

    std::string endProfile() {
        profiling.store(false, std::memory_order_release);
        int callbacks = 0;
        for (const auto& bucket : histogram)
            callbacks += bucket.load(std::memory_order_relaxed);
        int cumulative = 0;
        size_t p99 = 0;
        for (; p99 < histogram.size() - 1; ++p99) {
            cumulative += histogram[p99].load(std::memory_order_relaxed);
            if (cumulative * 100 >= 99 * callbacks)
                break;
        }
        return std::to_string(callbacks) + " callbacks at 16 voices | p99 " + std::to_string(p99)
            + "% | peak " + std::to_string(peak.load(std::memory_order_relaxed)) + "%";
    }

    void onErrorAfterClose(oboe::AudioStream*, oboe::Result result) override {
        error.store(result, std::memory_order_release);
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* device, void* buffer, int32_t frames) override {
        using Clock = std::chrono::steady_clock;
        const bool measure = profiling.load(std::memory_order_acquire);
        const auto start = measure ? Clock::now() : Clock::time_point {};
        queue.drain([&](auto event) { voice.apply(event); }, [&] { voice.releaseAll(); });
        voice.beginBlock();
        auto* output = static_cast<float*>(buffer);
        const int channels = device->getChannelCount();
        int32_t sounding = 0;
        float outputPeak = 0;
        const float target = fadingOut.load(std::memory_order_acquire) ? 0.0f : 1.0f;
        for (int32_t frame = 0; frame < frames; ++frame) {
            gain += std::clamp(target - gain, -fadeStep, fadeStep);
            auto sample = voice.nextFrame();
            sample.left *= gain;
            sample.right *= gain;
            const float peakValue = std::max(std::abs(sample.left), std::abs(sample.right));
            outputPeak = std::max(outputPeak, peakValue);
            if (peakValue > 0.0001f)
                ++sounding;
            if (channels == 1) *output++ = .5f * (sample.left + sample.right);
            else {
                *output++ = sample.left;
                *output++ = sample.right;
            }
        }
        audibleFrames.fetch_add(sounding, std::memory_order_relaxed);
        const int activeVoices = voice.soundingVoices();
        latestPeak.store(outputPeak, std::memory_order_relaxed);
        latestVoices.store(activeVoices, std::memory_order_relaxed);
        latestNote.store(voice.latestNote(), std::memory_order_relaxed);
        latestTick.store(voice.sequencePosition(), std::memory_order_relaxed);
        transport.store(voice.sequenceActivity(), std::memory_order_relaxed);
        const auto notes = voice.heldNotes();
        for (size_t index = 0; index < notes.size(); ++index) held[index].store(notes[index], std::memory_order_relaxed);
        if (measure && frames > 0 && activeVoices == ocarina::VoiceManager::maxVoices) {
            const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
            const int percent = static_cast<int>(std::ceil(elapsed * device->getSampleRate() * 100.0 / frames));
            histogram[std::min(percent, 100)].fetch_add(1, std::memory_order_relaxed);
            peak.store(std::max(peak.load(std::memory_order_relaxed), percent), std::memory_order_relaxed);
        }
        return oboe::DataCallbackResult::Continue;
    }

    std::atomic<int64_t> audibleFrames {0};
    std::atomic<float> latestPeak {0};
    std::atomic<int> latestVoices {0}, latestNote {-1};
    std::atomic<int> latestTick {0}, transport {0};
    std::array<std::atomic<uint32_t>, 4> held {};
    std::atomic<oboe::Result> error {oboe::Result::OK};
    std::atomic<bool> fadingOut {false};

private:
    ocarina::InstrumentEngine voice;
    ocarina::EventQueue queue;
    float gain = 0, fadeStep = 0;
    std::array<std::atomic<int>, 101> histogram {};
    std::atomic<int> peak {0};
    std::atomic<bool> profiling {false};
};

class AudioOutput final {
public:
    oboe::Result open(int mode, int requestedBursts, int deviceId, bool forceOpenSLES) {
        const int bursts = requestedBursts > 0 ? requestedBursts : mode == 0 ? 2 : mode == 2 ? 5 : 3;
        if (forceOpenSLES)
            return openStream(oboe::AudioApi::OpenSLES, oboe::SharingMode::Shared, bursts, oboe::kUnspecified);
        auto result = openStream(oboe::AudioApi::Unspecified,
            mode == 0 ? oboe::SharingMode::Exclusive : oboe::SharingMode::Shared, bursts, deviceId);
        if (result != oboe::Result::OK && mode == 0)
            result = openStream(oboe::AudioApi::Unspecified, oboe::SharingMode::Shared, bursts, deviceId);
        if (result != oboe::Result::OK)
            result = openStream(oboe::AudioApi::OpenSLES, oboe::SharingMode::Shared, bursts, oboe::kUnspecified);
        return result;
    }

    oboe::Result openStream(oboe::AudioApi backend, oboe::SharingMode sharing, int bursts, int deviceId) {
        renderer->error.store(oboe::Result::OK, std::memory_order_relaxed);
        oboe::AudioStreamBuilder builder;
        builder.setAudioApi(backend);
        builder.setDirection(oboe::Direction::Output);
        builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
        builder.setSharingMode(sharing);
        builder.setDeviceId(deviceId);
        builder.setUsage(oboe::Usage::Media);
        builder.setContentType(oboe::ContentType::Music);
        builder.setChannelCount(2);
        builder.setFormat(oboe::AudioFormat::Float);
        builder.setFormatConversionAllowed(true);
        builder.setDataCallback(renderer);
        builder.setErrorCallback(renderer);
        const auto result = builder.openStream(stream);
        if (result != oboe::Result::OK)
            return result;
        rate = stream->getSampleRate();
        api = stream->getAudioApi();
        burstFrames = stream->getFramesPerBurst();
        if (burstFrames > 0)
            stream->setBufferSizeInFrames(std::min(burstFrames * bursts, stream->getBufferCapacityInFrames()));
        renderer->prepare(rate);
        const auto started = stream->requestStart();
        if (started != oboe::Result::OK) {
            stream->close();
            stream.reset();
        }
        return started;
    }

    ~AudioOutput() {
        if (stream)
            stream->close();
    }

    int xRuns() const noexcept {
        const auto result = stream->getXRunCount();
        return result ? result.value() : -1;
    }
    int bufferFrames() const noexcept { return stream->getBufferSizeInFrames(); }

    const std::shared_ptr<Renderer> renderer = std::make_shared<Renderer>();
    int rate = 0;
    int burstFrames = 0;
    oboe::AudioApi api = oboe::AudioApi::Unspecified;

private:
    std::shared_ptr<oboe::AudioStream> stream;
};

AudioOutput* output(jlong handle) noexcept { return reinterpret_cast<AudioOutput*>(handle); }

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_blinkzer0_ocarina_MainActivity_openAudio(JNIEnv* env, jobject, jint mode, jint bursts, jint deviceId, jboolean forceOpenSLES) {
    auto instance = std::make_unique<AudioOutput>();
    const auto result = instance->open(mode, bursts, deviceId, forceOpenSLES);
    if (result != oboe::Result::OK) {
        env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), oboe::convertToText(result));
        return 0;
    }
    return reinterpret_cast<jlong>(instance.release());
}

extern "C" JNIEXPORT void JNICALL
Java_com_blinkzer0_ocarina_MainActivity_closeAudio(JNIEnv*, jobject, jlong handle) {
    delete output(handle);
}

extern "C" JNIEXPORT void JNICALL
Java_com_blinkzer0_ocarina_MainActivity_fadeAudio(JNIEnv*, jobject, jlong handle) {
    output(handle)->renderer->fadingOut.store(true, std::memory_order_release);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_blinkzer0_ocarina_MainActivity_audioFailed(JNIEnv*, jobject, jlong handle) {
    return output(handle)->renderer->error.load(std::memory_order_acquire) != oboe::Result::OK;
}

extern "C" JNIEXPORT void JNICALL
Java_com_blinkzer0_ocarina_MainActivity_sendNote(JNIEnv*, jobject, jlong handle, jboolean on, jint key, jint source, jfloat velocity) {
    output(handle)->renderer->note(on, key, source, velocity);
}

extern "C" JNIEXPORT void JNICALL
Java_com_blinkzer0_ocarina_MainActivity_sendMidi(JNIEnv*, jobject, jlong handle, jint source, jint status, jint first, jint second) {
    output(handle)->renderer->midi(source, status, first, second);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_blinkzer0_ocarina_MainActivity_setSequence(JNIEnv* env, jobject, jlong handle, jintArray input) {
    const auto count = env->GetArrayLength(input);
    if (count > static_cast<int>(ocarina::Sequencer::packetSize)) return false;
    std::array<int32_t, ocarina::Sequencer::packetSize> packet;
    env->GetIntArrayRegion(input, 0, count, packet.data());
    return !env->ExceptionCheck() && output(handle)->renderer->sequence(std::span(packet.data(), static_cast<size_t>(count)));
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_blinkzer0_ocarina_MainActivity_performanceState(JNIEnv* env, jobject, jlong handle) {
    const auto& renderer = *output(handle)->renderer;
    std::array<jint, 6> values {renderer.latestTick.load(std::memory_order_relaxed), renderer.transport.load(std::memory_order_relaxed)};
    for (size_t index = 0; index < renderer.held.size(); ++index)
        values[index + 2] = static_cast<jint>(renderer.held[index].load(std::memory_order_relaxed));
    const auto result = env->NewIntArray(values.size());
    if (result != nullptr) env->SetIntArrayRegion(result, 0, values.size(), values.data());
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_blinkzer0_ocarina_MainActivity_setPerformance(JNIEnv*, jobject, jlong handle, jint model, jint voices) {
    output(handle)->renderer->performance(model, voices);
}

extern "C" JNIEXPORT void JNICALL
Java_com_blinkzer0_ocarina_MainActivity_setParameter(JNIEnv*, jobject, jlong handle, jint index, jfloat value) {
    output(handle)->renderer->parameter(index, value);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_blinkzer0_ocarina_MainActivity_parameterCount(JNIEnv*, jobject) { return ocarina::parameterCount; }

extern "C" JNIEXPORT jboolean JNICALL
Java_com_blinkzer0_ocarina_MainActivity_setPatch(JNIEnv* env, jobject, jlong handle, jfloatArray input) {
    std::array<float, ocarina::parameterCount> values;
    if (env->GetArrayLength(input) != static_cast<jsize>(values.size())) return false;
    env->GetFloatArrayRegion(input, 0, static_cast<jsize>(values.size()), values.data());
    return !env->ExceptionCheck() && output(handle)->renderer->patch(values);
}

extern "C" JNIEXPORT void JNICALL
Java_com_blinkzer0_ocarina_MainActivity_releaseKeyboard(JNIEnv*, jobject, jlong handle, jint source) {
    output(handle)->renderer->release(static_cast<uint16_t>(source));
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_blinkzer0_ocarina_MainActivity_telemetry(JNIEnv* env, jobject, jlong handle) {
    const auto& renderer = *output(handle)->renderer;
    const jfloat state[] { renderer.latestPeak.load(std::memory_order_relaxed),
        static_cast<jfloat>(renderer.latestVoices.load(std::memory_order_relaxed)),
        static_cast<jfloat>(renderer.latestNote.load(std::memory_order_relaxed)) };
    auto array = env->NewFloatArray(3);
    if (array)
        env->SetFloatArrayRegion(array, 0, 3, state);
    return array;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_blinkzer0_ocarina_MainActivity_beginProfile(JNIEnv*, jobject, jlong handle) {
    auto& device = *output(handle);
    device.renderer->beginProfile();
    return device.xRuns();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_blinkzer0_ocarina_MainActivity_endProfile(JNIEnv* env, jobject, jlong handle, jint before) {
    auto& device = *output(handle);
    auto report = device.renderer->endProfile();
    const int after = device.xRuns();
    report += before >= 0 && after >= 0 ? " | xruns " + std::to_string(after - before) : " | xruns unavailable";
    return env->NewStringUTF(report.c_str());
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_blinkzer0_ocarina_MainActivity_audibleFrames(JNIEnv*, jobject, jlong handle) {
    return output(handle)->renderer->audibleFrames.load(std::memory_order_relaxed);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_blinkzer0_ocarina_MainActivity_audioDescription(JNIEnv* env, jobject, jlong handle) {
    const auto& device = *output(handle);
    const auto error = device.renderer->error.load(std::memory_order_acquire);
    if (error != oboe::Result::OK)
        return env->NewStringUTF(oboe::convertToText(error));
    const auto description = std::string(oboe::convertToText(device.api)) + " · " + std::to_string(device.rate) + " Hz · "
        + std::to_string(device.bufferFrames()) + " frames (" + std::to_string(device.burstFrames) + "/burst)";
    return env->NewStringUTF(description.c_str());
}
