#include <jni.h>
#include <string>
#include <android/log.h>
#include <oboe/Oboe.h>
#include <random>
#include <cmath>
#include <atomic>

#define LOG_TAG "AcousticDSP"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define FFT_BANDS 64

class AudioEngine : public oboe::AudioStreamCallback {
private:
    std::shared_ptr<oboe::AudioStream> mOutputStream;
    std::shared_ptr<oboe::AudioStream> mInputStream;
    std::mt19937 gen;
    std::uniform_real_distribution<float> dis;
    
    float fftOutput[FFT_BANDS];
    
    // Баланс: 0.0 = только шум, 1.0 = только микрофон. По умолчанию 0.5
    std::atomic<float> mixRatio_{0.5f};

public:
    AudioEngine() : gen(std::random_device{}()), dis(-1.0f, 1.0f) {
        for(int i=0; i<FFT_BANDS; i++) fftOutput[i] = 0.0f;
    }

    void setMix(float ratio) {
        mixRatio_.store(ratio, std::memory_order_relaxed);
    }

    bool start() {
        oboe::AudioStreamBuilder inputBuilder;
        inputBuilder.setDirection(oboe::Direction::Input)
                    ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
                    ->setSharingMode(oboe::SharingMode::Shared)
                    ->setFormat(oboe::AudioFormat::Float)
                    ->setChannelCount(oboe::ChannelCount::Mono);
        
        oboe::Result result = inputBuilder.openStream(mInputStream);
        if (result != oboe::Result::OK) return false;

        oboe::AudioStreamBuilder outputBuilder;
        outputBuilder.setDirection(oboe::Direction::Output)
                     ->setPerformanceMode(oboe::PerformanceMode::LowLatency)
                     ->setSharingMode(oboe::SharingMode::Exclusive)
                     ->setFormat(oboe::AudioFormat::Float)
                     ->setChannelCount(oboe::ChannelCount::Mono)
                     ->setDataCallback(this);

        result = outputBuilder.openStream(mOutputStream);
        if (result != oboe::Result::OK) return false;

        mInputStream->requestStart();
        mOutputStream->requestStart();
        return true;
    }

    void stop() {
        if (mInputStream) { mInputStream->stop(); mInputStream->close(); mInputStream.reset(); }
        if (mOutputStream) { mOutputStream->stop(); mOutputStream->close(); mOutputStream.reset(); }
    }

    void getFftData(float* outArray) {
        for (int i = 0; i < FFT_BANDS; ++i) outArray[i] = fftOutput[i];
    }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *oboeStream, void *audioData, int32_t numFrames) override {
        auto *floatData = static_cast<float *>(audioData);
        
        float micData[1024];
        int32_t framesRead = 0;
        if (mInputStream) {
            auto result = mInputStream->read(micData, numFrames, 0); 
            if (result) framesRead = result.value();
        }

        // Читаем текущее значение ползунка (от 0.0 до 1.0)
        float currentMix = mixRatio_.load(std::memory_order_relaxed);
        float micVolume = currentMix;         // Если ползунок справа - микрофон громче
        float noiseVolume = 1.0f - currentMix; // Если ползунок слева - шум громче

        for (int i = 0; i < numFrames; ++i) {
            float mic = (i < framesRead) ? micData[i] : 0.0f;
            float noise = dis(gen) * 0.1f; 
            
            // Смешиваем с учетом ползунка
            floatData[i] = (noise * noiseVolume) + (mic * micVolume); 
        }

        // FFT расчет (как в прошлом шаге)
        int analyzeFrames = (numFrames < 512) ? numFrames : 512;
        if (analyzeFrames > 0) {
            for (int k = 0; k < FFT_BANDS; ++k) {
                float real = 0, imag = 0;
                float frequency = (float)k / FFT_BANDS; 
                for (int n = 0; n < analyzeFrames; ++n) {
                    float angle = 2.0f * M_PI * frequency * n;
                    real += floatData[n] * cosf(angle);
                    imag -= floatData[n] * sinf(angle);
                }
                float magnitude = sqrtf(real*real + imag*imag) / analyzeFrames;
                fftOutput[k] = (magnitude > 0) ? (20.0f * log10f(magnitude) + 100.0f) : 0.0f;
                if (fftOutput[k] < 0) fftOutput[k] = 0;
                if (fftOutput[k] > 100) fftOutput[k] = 100;
            }
        }
        return oboe::DataCallbackResult::Continue;
    }
};

AudioEngine engine;

extern "C" JNIEXPORT jstring JNICALL
Java_com_uw5ent_acousticlab_MainActivity_getDspStatus(JNIEnv* env, jobject) {
    return env->NewStringUTF("DSP Engine: Ready. FFT Active.");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_uw5ent_acousticlab_MainActivity_startAudio(JNIEnv* env, jobject) {
    return engine.start();
}

extern "C" JNIEXPORT void JNICALL
Java_com_uw5ent_acousticlab_MainActivity_stopAudio(JNIEnv* env, jobject) {
    engine.stop();
}

extern "C" JNIEXPORT void JNICALL
Java_com_uw5ent_acousticlab_MainActivity_getFftData(JNIEnv* env, jobject, jfloatArray outArray) {
    jfloat* buffer = env->GetFloatArrayElements(outArray, 0);
    engine.getFftData(buffer);
    env->ReleaseFloatArrayElements(outArray, buffer, 0);
}

// НОВЫЙ МЕТОД ДЛЯ УПРАВЛЕНИЯ ПОЛЗУНКОМ
extern "C" JNIEXPORT void JNICALL
Java_com_uw5ent_acousticlab_MainActivity_setMixRatio(JNIEnv* env, jobject, jfloat ratio) {
    engine.setMix(ratio);
}
