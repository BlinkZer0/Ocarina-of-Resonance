#include "dsp/PrototypeVoice.h"

#include <jni.h>
#include <oboe/Oboe.h>

#include <atomic>
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

    void onErrorAfterClose(oboe::AudioStream*, oboe::Result result) override {
        error.store(result, std::memory_order_release);
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream* device, void* buffer, int32_t frames) override {
        queue.drain([&](auto event) { voice.apply(event); }, [&] { voice.releaseAll(); });
        auto* output = static_cast<float*>(buffer);
        const int channels = device->getChannelCount();
        int32_t sounding = 0;
        for (int32_t frame = 0; frame < frames; ++frame) {
            const float sample = voice.nextSample();
            if (std::abs(sample) > 0.0001f)
                ++sounding;
            for (int channel = 0; channel < channels; ++channel)
                *output++ = sample;
        }
        audibleFrames.fetch_add(sounding, std::memory_order_relaxed);
        return oboe::DataCallbackResult::Continue;
    }

    std::atomic<int64_t> audibleFrames {0};
    std::atomic<oboe::Result> error {oboe::Result::OK};

private:
    ocarina::PrototypeVoice voice;
    ocarina::EventQueue queue;
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
