#include <jni.h>
#include <string>
#include <android/log.h>
#include <oboe/Oboe.h>
#include <random>
#include <cmath>
#include <atomic>
#include <vector>

#define LOG_TAG "AcousticDSP"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define FFT_SIZE 512
#define FFT_BANDS 64

class AudioEngine : public oboe::AudioStreamCallback {
private:
    std::shared_ptr<oboe::AudioStream> mOutputStream;
    std::shared_ptr<oboe::AudioStream> mInputStream;
    std::mt19937 gen;
    std::uniform_real_distribution<float> dis;
    
    float fftOutput[FFT_BANDS];
    std::atomic<float> mixRatio_{0.5f};
    std::atomic<bool> filterEnabled_{true};
    
    // Параметры для Noise Reduction
    std::atomic<bool> isLearning_{false};
    std::atomic<bool> isReducing_{false};
    std::vector<float> noiseProfile; // Профиль шума
    int learnFramesCounter = 0;

    // Буферы для STFT (Overlap-Add)
    std::vector<float> inputBuffer;
    std::vector<float> outputBuffer;
    int bufferIndex = 0;

    // Фильтры (Bandpass и Notch)
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
    Biquad bandpass, notch50, notch100;

public:
    AudioEngine() : gen(std::random_device{}()), dis(-1.0f, 1.0f) {
        for(int i=0; i<FFT_BANDS; i++) fftOutput[i] = 0.0f;
        noiseProfile.resize(FFT_SIZE / 2 + 1, 0.0f);
        inputBuffer.resize(FFT_SIZE, 0.0f);
        outputBuffer.resize(FFT_SIZE, 0.0f);
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
    }

    void setMix(float ratio) { mixRatio_.store(ratio, std::memory_order_relaxed); }
    void setFilterEnabled(bool enabled) { filterEnabled_.store(enabled, std::memory_order_relaxed); }
    void startLearning() { isLearning_.store(true, std::memory_order_relaxed); learnFramesCounter = 0; }
    void setReducing(bool enabled) { isReducing_.store(enabled, std::memory_order_relaxed); }

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
        float micVol = currentMix;
        float noiseVol = 1.0f - currentMix;
        bool useFilters = filterEnabled_.load(std::memory_order_relaxed);
        bool reducing = isReducing_.load(std::memory_order_relaxed);

        for (int i = 0; i < numFrames; ++i) {
            float mic = (i < framesRead) ? micData[i] : 0.0f;
            float noise = dis(gen) * 0.1f; 
            float mixed = (noise * noiseVol) + (mic * micVol); 

            if (useFilters) {
                mixed = notch50.process(mixed);
                mixed = bandpass.process(mixed);
            }

            // --- SPECTRAL SUBTRACTION (STFT) ---
            inputBuffer[bufferIndex] = mixed;
            float outSample = outputBuffer[bufferIndex];
            bufferIndex++;

            if (bufferIndex >= FFT_SIZE) {
                bufferIndex = 0;
                processSTFT(reducing, isLearning_.load(std::memory_order_relaxed));
                if (isLearning_.load(std::memory_order_relaxed)) {
                    learnFramesCounter++;
                    if (learnFramesCounter > 20) isLearning_.store(false, std::memory_order_relaxed); // ~1 секунда
                }
            }
            floatData[i] = outSample;
        }

        // Обновляем визуализатор
        updateVisualizer(floatData, numFrames);
        return oboe::DataCallbackResult::Continue;
    }

    void processSTFT(bool reducing, bool learning) {
        // Применяем оконную функцию Ханна (чтобы не было щелчков)
        for (int i = 0; i < FFT_SIZE; ++i) {
            float w = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
            inputBuffer[i] *= w;
        }

        // Здесь упрощенный DFT/IDFT (чтобы не подключать сторонние библиотеки)
        std::vector<float> real(FFT_SIZE, 0), imag(FFT_SIZE, 0);
        for (int k = 0; k < FFT_SIZE / 2 + 1; ++k) {
            for (int n = 0; n < FFT_SIZE; ++n) {
                float angle = 2.0f * M_PI * k * n / FFT_SIZE;
                real[k] += inputBuffer[n] * cosf(angle);
                imag[k] -= inputBuffer[n] * sinf(angle);
            }
            float mag = sqrtf(real[k]*real[k] + imag[k]*imag[k]);
            
            if (learning) {
                noiseProfile[k] = std::max(noiseProfile[k], mag); // Запоминаем максимум шума
            } else if (reducing) {
                // Вычитаем шум (оставляем минимум 5% от оригинала, чтобы не было "музыкального шума")
                float cleanMag = std::max(mag - noiseProfile[k], mag * 0.05f);
                float phase = atan2f(imag[k], real[k]);
                real[k] = cleanMag * cosf(phase);
                imag[k] = cleanMag * sinf(phase);
            }
        }

        // Обратное преобразование (IDFT)
        for (int n = 0; n < FFT_SIZE; ++n) {
            float val = 0;
            for (int k = 0; k < FFT_SIZE; ++k) {
                float angle = 2.0f * M_PI * k * n / FFT_SIZE;
                val += real[k] * cosf(angle) - imag[k] * sinf(angle);
            }
            val /= FFT_SIZE;
            // Overlap-Add (сохраняем во второй половине, чтобы избежать разрывов)
            outputBuffer[n] = val * 0.5f; 
        }
    }

    void updateVisualizer(float* data, int numFrames) {
        int analyzeFrames = (numFrames < 256) ? numFrames : 256;
        for (int k = 0; k < FFT_BANDS; ++k) {
            float real = 0, imag = 0;
            float freq = (float)k / FFT_BANDS;
            for (int n = 0; n < analyzeFrames; ++n) {
                float angle = 2.0f * M_PI * freq * n;
                real += data[n] * cosf(angle);
                imag -= data[n] * sinf(angle);
            }
            float mag = sqrtf(real*real + imag*imag) / analyzeFrames;
            fftOutput[k] = (mag > 0) ? (20.0f * log10f(mag) + 100.0f) : 0.0f;
            if (fftOutput[k] < 0) fftOutput[k] = 0;
            if (fftOutput[k] > 100) fftOutput[k] = 100;
        }
    }
};

AudioEngine engine;

extern "C" JNIEXPORT jstring JNICALL
Java_com_uw5ent_acousticlab_MainActivity_getDspStatus(JNIEnv* env, jobject) {
    return env->NewStringUTF("DSP Engine: Ready. Noise Reduction Active.");
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

extern "C" JNIEXPORT void JNICALL
Java_com_uw5ent_acousticlab_MainActivity_startLearning(JNIEnv* env, jobject) { engine.startLearning(); }

extern "C" JNIEXPORT void JNICALL
Java_com_uw5ent_acousticlab_MainActivity_setNoiseReduction(JNIEnv* env, jobject, jboolean enabled) { engine.setReducing(enabled); }
