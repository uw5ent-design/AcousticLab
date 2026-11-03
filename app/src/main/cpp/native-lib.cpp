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

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    float process(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

class AudioEngine : public oboe::AudioStreamCallback {
private:
    std::shared_ptr<oboe::AudioStream> mOutputStream;
    std::shared_ptr<oboe::AudioStream> mInputStream;
    std::mt19937 gen;
    std::uniform_real_distribution<float> dis;
    
    float fftOutput[FFT_BANDS];
    std::atomic<float> mixRatio_{0.5f};
    std::atomic<bool> filterEnabled_{true};

    Biquad bandpass, notch50, notch100;

public:
    AudioEngine() : gen(std::random_device{}()), dis(-1.0f, 1.0f) {
        for(int i=0; i<FFT_BANDS; i++) fftOutput[i] = 0.0f;
        initFilters();
    }

    void initFilters() {
        float fs = 48000.0f;
        float fc = sqrt(300.0f * 3400.0f);
        float bw = 3400.0f - 300.0f;
        float Q = fc / bw;
        float w0 = 2.0f * M_PI * fc / fs;
        float alpha = sin(w0) / (2.0f * Q);
        float a0 = 1.0f + alpha;
        bandpass.b0 = alpha / a0; bandpass.b1 = 0; bandpass.b2 = -alpha / a0;
        bandpass.a1 = -2.0f * cos(w0) / a0; bandpass.a2 = (1.0f - alpha) / a0;

        float f0_50 = 50.0f;
        float w0_50 = 2.0f * M_PI * f0_50 / fs;
        float alpha_50 = sin(w0_50) / (2.0f * 5.0f);
        float a0_50 = 1.0f + alpha_50;
        notch50.b0 = 1.0f / a0_50; notch50.b1 = -2.0f * cos(w0_50) / a0_50; notch50.b2 = 1.0f / a0_50;
        notch50.a1 = -2.0f * cos(w0_50) / a0_50; notch50.a2 = (1.0f - alpha_50) / a0_50;
        
        float f0_100 = 100.0f;
        float w0_100 = 2.0f * M_PI * f0_100 / fs;
        float alpha_100 = sin(w0_100) / (2.0f * 5.0f);
        float a0_100 = 1.0f + alpha_100;
        notch100.b0 = 1.0f / a0_100; notch100.b1 = -2.0f * cos(w0_100) / a0_100; notch100.b2 = 1.0f / a0_100;
        notch100.a1 = -2.0f * cos(w0_100) / a0_100; notch100.a2 = (1.0f - alpha_100) / a0_100;
    }

    void setMix(float ratio) { mixRatio_.store(ratio, std::memory_order_relaxed); }
    void setFilterEnabled(bool enabled) { filterEnabled_.store(enabled, std::memory_order_relaxed); }

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

        float currentMix = mixRatio_.load(std::memory_order_relaxed);
        float micVolume = currentMix;
        float noiseVolume = 1.0f - currentMix;
        bool useFilters = filterEnabled_.load(std::memory_order_relaxed);

        for (int i = 0; i < numFrames; ++i) {
            float mic = (i < framesRead) ? micData[i] : 0.0f;
            float noise = dis(gen) * 0.1f; 
            float mixed = (noise * noiseVolume) + (mic * micVolume); 

            if (useFilters) {
                mixed = notch50.process(mixed);
                mixed = notch100.process(mixed);
                mixed = bandpass.process(mixed);
            }
            floatData[i] = mixed;
        }

        // Легкий расчет FFT для визуализации (без подвисаний)
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
    return env->NewStringUTF("DSP Engine: Stable Mode.");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_uw5ent_acousticlab_MainActivity_startAudio(JNIEnv* env, jobject) { return engine.start(); }

extern "C" JNIEXPORT void JNICALL
Java_com_uw5ent_acousticlab_MainActivity_stopAudio(JNIEnv* env, jobject) { engine.stop(); }

extern "C" JNIEXPORT void JNICALL
Java_com_uw5ent_acousticlab_MainActivity_getFftData(JNIEnv* env, jobject, jfloatArray outArray) {
    jfloat* buffer = env->GetFloatArrayElements(outArray, 0);
    engine.getFftData(buffer);
    env->ReleaseFloatArrayElements(outArray, buffer, 0);
}

extern "C" JNIEXPORT void JNICALL
Java_com_uw5ent_acousticlab_MainActivity_setMixRatio(JNIEnv* env, jobject, jfloat ratio) { engine.setMix(ratio); }

extern "C" JNIEXPORT void JNICALL
Java_com_uw5ent_acousticlab_MainActivity_setFilterEnabled(JNIEnv* env, jobject, jboolean enabled) { engine.setFilterEnabled(enabled); }

// Заглушки для кнопок, чтобы приложение не вылетало (пока)
extern "C" JNIEXPORT void JNICALL
Java_com_uw5ent_acousticlab_MainActivity_startLearning(JNIEnv* env, jobject) { }

extern "C" JNIEXPORT void JNICALL
Java_com_uw5ent_acousticlab_MainActivity_setNoiseReduction(JNIEnv* env, jobject, jboolean enabled) { }
