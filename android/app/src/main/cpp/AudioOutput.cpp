#include "dsp/VoiceManager.h"

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
    void prepare(int rate) noexcept { voice.prepare(rate); }

    void note(bool on, int key, int source) noexcept {
        if (key >= 0 && key <= 127 && source >= 0 && source <= 65535) {
            queue.push({on ? ocarina::EventType::noteOn : ocarina::EventType::noteOff,
                static_cast<uint16_t>(source), 0, static_cast<uint8_t>(key), 0.8f});
        }
    }

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
        auto* output = static_cast<float*>(buffer);
        const int channels = device->getChannelCount();
        int32_t sounding = 0;
        float outputPeak = 0;
        for (int32_t frame = 0; frame < frames; ++frame) {
            const float sample = voice.nextSample();
            outputPeak = std::max(outputPeak, std::abs(sample));
            if (std::abs(sample) > 0.0001f)
                ++sounding;
            for (int channel = 0; channel < channels; ++channel)
                *output++ = sample;
        }
        audibleFrames.fetch_add(sounding, std::memory_order_relaxed);
        const int activeVoices = voice.soundingVoices();
        latestPeak.store(outputPeak, std::memory_order_relaxed);
        latestVoices.store(activeVoices, std::memory_order_relaxed);
        latestNote.store(voice.latestNote(), std::memory_order_relaxed);
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
    std::atomic<oboe::Result> error {oboe::Result::OK};

private:
    ocarina::VoiceManager voice;
    ocarina::EventQueue queue;
    std::array<std::atomic<int>, 101> histogram {};
    std::atomic<int> peak {0};
    std::atomic<bool> profiling {false};
};

class AudioOutput final {
public:
    oboe::Result open() {
        oboe::AudioStreamBuilder builder;
        builder.setDirection(oboe::Direction::Output);
        builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
        builder.setSharingMode(oboe::SharingMode::Shared);
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
        renderer->prepare(rate);
        return stream->requestStart();
    }

    ~AudioOutput() {
        if (stream)
            stream->close();
    }

    int xRuns() const noexcept {
        const auto result = stream->getXRunCount();
        return result ? result.value() : -1;
    }

    const std::shared_ptr<Renderer> renderer = std::make_shared<Renderer>();
    int rate = 0;
    oboe::AudioApi api = oboe::AudioApi::Unspecified;

private:
    std::shared_ptr<oboe::AudioStream> stream;
};

AudioOutput* output(jlong handle) noexcept { return reinterpret_cast<AudioOutput*>(handle); }

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_blinkzer0_ocarina_MainActivity_openAudio(JNIEnv* env, jobject) {
    auto instance = std::make_unique<AudioOutput>();
    const auto result = instance->open();
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
Java_com_blinkzer0_ocarina_MainActivity_sendNote(JNIEnv*, jobject, jlong handle, jboolean on, jint key, jint source) {
    output(handle)->renderer->note(on, key, source);
}

extern "C" JNIEXPORT void JNICALL
Java_com_blinkzer0_ocarina_MainActivity_setPerformance(JNIEnv*, jobject, jlong handle, jint model, jint voices) {
    output(handle)->renderer->performance(model, voices);
}

extern "C" JNIEXPORT void JNICALL
Java_com_blinkzer0_ocarina_MainActivity_setParameter(JNIEnv*, jobject, jlong handle, jint index, jfloat value) {
    output(handle)->renderer->parameter(index, value);
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
    const auto description = std::string(oboe::convertToText(device.api)) + " · " + std::to_string(device.rate) + " Hz";
    return env->NewStringUTF(description.c_str());
}
