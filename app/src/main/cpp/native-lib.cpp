#include <jni.h>
#include <string>
#include <android/log.h>
#include <oboe/Oboe.h>
#include <random>

#define LOG_TAG "AcousticDSP"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

class AudioEngine : public oboe::AudioStreamCallback {
private:
    std::shared_ptr<oboe::AudioStream> mStream;
    std::mt19937 gen;
    std::uniform_real_distribution<float> dis;

public:
    AudioEngine() : gen(std::random_device{}()), dis(-1.0f, 1.0f) {}

    bool start() {
        oboe::AudioStreamBuilder builder;
        builder.setDirection(oboe::Direction::Output)
               ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
               ->setSharingMode(oboe::SharingMode::Exclusive)
               ->setFormat(oboe::AudioFormat::Float)
               ->setChannelCount(oboe::ChannelCount::Mono)
               ->setDataCallback(this);

        oboe::Result result = builder.openStream(mStream);
        if (result != oboe::Result::OK) {
            LOGI("Error opening stream: %s", oboe::convertToText(result));
            return false;
        }

        mStream->requestStart();
        LOGI("Audio Engine Started");
        return true;
    }

    void stop() {
        if (mStream) {
            mStream->stop();
            mStream->close();
            mStream.reset();
            LOGI("Audio Engine Stopped");
        }
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *oboeStream, void *audioData, int32_t numFrames) override {
        auto *floatData = static_cast<float *>(audioData);
        
        // Генерируем белый шум (Канал А)
        for (int i = 0; i < numFrames; ++i) {
            floatData[i] = dis(gen) * 0.1f; // 0.1f - это громкость
        }
        
        return oboe::DataCallbackResult::Continue;
    }
};

// Создаем глобальный экземпляр движка
AudioEngine engine;

extern "C" JNIEXPORT jstring JNICALL
Java_com_uw5ent_acousticlab_MainActivity_getDspStatus(JNIEnv* env, jobject /* this */) {
    std::string status = "DSP Engine: Ready. Oboe initialized.";
    return env->NewStringUTF(status.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_uw5ent_acousticlab_MainActivity_startAudio(JNIEnv* env, jobject /* this */) {
    return engine.start();
}

extern "C" JNIEXPORT void JNICALL
Java_com_uw5ent_acousticlab_MainActivity_stopAudio(JNIEnv* env, jobject /* this */) {
    engine.stop();
}
