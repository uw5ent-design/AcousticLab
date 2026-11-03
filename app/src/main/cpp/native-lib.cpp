#include <jni.h>
#include <string>
#include <android/log.h>
#include <oboe/Oboe.h>
#include <random>

#define LOG_TAG "AcousticDSP"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

class AudioEngine : public oboe::AudioStreamCallback {
private:
    std::shared_ptr<oboe::AudioStream> mOutputStream;
    std::shared_ptr<oboe::AudioStream> mInputStream;
    std::mt19937 gen;
    std::uniform_real_distribution<float> dis;

public:
    AudioEngine() : gen(std::random_device{}()), dis(-1.0f, 1.0f) {}

    bool start() {
        // 1. Настраиваем ВХОД (Микрофон - Канал Б)
        oboe::AudioStreamBuilder inputBuilder;
        inputBuilder.setDirection(oboe::Direction::Input)
                    ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
                    ->setSharingMode(oboe::SharingMode::Shared) // Shared надежнее для одновременной работы с выходом
                    ->setFormat(oboe::AudioFormat::Float)
                    ->setChannelCount(oboe::ChannelCount::Mono);
        
        oboe::Result result = inputBuilder.openStream(mInputStream);
        if (result != oboe::Result::OK) {
            LOGI("Error opening input stream: %s", oboe::convertToText(result));
            return false;
        }

        // 2. Настраиваем ВЫХОД (Динамик)
        oboe::AudioStreamBuilder outputBuilder;
        outputBuilder.setDirection(oboe::Direction::Output)
                     ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
                     ->setSharingMode(oboe::SharingMode::Exclusive)
                     ->setFormat(oboe::AudioFormat::Float)
                     ->setChannelCount(oboe::ChannelCount::Mono)
                     ->setDataCallback(this);

        result = outputBuilder.openStream(mOutputStream);
        if (result != oboe::Result::OK) {
            LOGI("Error opening output stream: %s", oboe::convertToText(result));
            return false;
        }

        mInputStream->requestStart();
        mOutputStream->requestStart();
        LOGI("Audio Engine Started (Mic + Noise)");
        return true;
    }

    void stop() {
        if (mInputStream) { mInputStream->stop(); mInputStream->close(); mInputStream.reset(); }
        if (mOutputStream) { mOutputStream->stop(); mOutputStream->close(); mOutputStream.reset(); }
        LOGI("Audio Engine Stopped");
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *oboeStream, void *audioData, int32_t numFrames) override {
        auto *floatData = static_cast<float *>(audioData);
        
        // Читаем данные с микрофона
        float micData[1024];
        int32_t framesRead = 0;
        if (mInputStream) {
            // ВОТ ЗДЕСЬ ИСПРАВЛЕНИЕ: сохраняем результат в result
            auto result = mInputStream->read(micData, numFrames, 0); 
            if (result) {
                framesRead = result.value();
            }
        }

        // Смешиваем: Микрофон + Белый шум
        for (int i = 0; i < numFrames; ++i) {
            float mic = (i < framesRead) ? micData[i] : 0.0f;
            float noise = dis(gen) * 0.05f; 
            
            // Канал А (шум) * 0.3 + Канал Б (микрофон) * 0.7 
            // Сделал микрофон громче, чтобы ты его точно услышал
            floatData[i] = (noise * 0.3f) + (mic * 0.7f); 
        }
        
        return oboe::DataCallbackResult::Continue;
    }
};

AudioEngine engine;

extern "C" JNIEXPORT jstring JNICALL
Java_com_uw5ent_acousticlab_MainActivity_getDspStatus(JNIEnv* env, jobject /* this */) {
    std::string status = "DSP Engine: Ready. Mic + Noise.";
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
