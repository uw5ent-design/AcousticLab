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

// Уменьшили размер FFT для скорости, 256 сэмплов = ~5 мс
#define FFT_SIZE 256
#define HOP_SIZE 128
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
    
    // Переменные для Noise Reduction
    std::atomic<bool> isLearning_{false};
    std::atomic<bool> isReducing_{false};
    std::vector<float> noiseProfile;
    int learnFramesCounter = 0;

    // Буферы и таблицы для БЫСТРОГО STFT
    std::vector<float> inputBuffer;
    std::vector<float> overlapBuffer;
    int bufferIndex = 0;

    float dft_cos[FFT_SIZE][FFT_SIZE];
    float dft_sin[FFT_SIZE][FFT_SIZE];
    float window[FFT_SIZE];

    // Фильтры
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
    Biquad bandpass, notch50;

public:
    AudioEngine() : gen(std::random_device{}()), dis(-1.0f, 1.0f) {
        for(int i=0; i<FFT_BANDS; i++) fftOutput[i] = 0.0f;
        noiseProfile.resize(FFT_SIZE / 2 + 1, 0.0f);
        inputBuffer.resize(FFT_SIZE, 0.0f);
        overlapBuffer.resize(FFT_SIZE, 0.0f);
        
        // Предрасчет окон и таблиц синусов/косинусов (для ускорения)
        for (int i = 0; i < FFT_SIZE; ++i) {
            window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
            for (int j = 0; j < FFT_SIZE; ++j) {
                float angle = 2.0f * M_PI * i * j / FFT_SIZE;
                dft_cos[i][j] = cosf(angle);
                dft_sin[i][j] = -sinf(angle);
            }
        }
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
        bool useFilters = filterEnabled_.load(std::memory_order_relaxed);
        bool reducing = isReducing_.load(std::memory_order_relaxed);

        for (int i = 0; i < numFrames; ++i) {
            float mic = (i < framesRead) ? micData[i] : 0.0f;
            float mixed = mic * currentMix; 

            if (useFilters) {
                mixed = notch50.process(mixed);
                mixed = bandpass.process(mixed);
            }

            // --- ПРАВИЛЬНЫЙ OVERLAP-ADD ---
            inputBuffer[bufferIndex] = mixed;
            
            // Берем уже готовый сэмпл из перекрытия и очищаем место для будущего
            float outSample = overlapBuffer[bufferIndex];
            overlapBuffer[bufferIndex] = 0.0f; 
            
            bufferIndex++;

            // Если набрали HOP_SIZE, запускаем просчет блока FFT_SIZE
            if (bufferIndex >= HOP_SIZE) {
                processSTFT(reducing, isLearning_.load(std::memory_order_relaxed));
                if (isLearning_.load(std::memory_order_relaxed)) {
                    learnFramesCounter++;
                    if (learnFramesCounter > 50) isLearning_.store(false, std::memory_order_relaxed); // ~2 сек
                }
                bufferIndex = 0;
            }
            
            // Умножаем на 4.0f, чтобы компенсировать тишину после FFT
            floatData[i] = outSample * 4.0f; 
        }

        updateVisualizer(floatData, numFrames);
        return oboe::DataCallbackResult::Continue;
    }

    void processSTFT(bool reducing, bool learning) {
        // Копируем последние FFT_SIZE сэмплов и применяем окно
        std::vector<float> fftBuf(FFT_SIZE, 0.0f);
        for (int i = 0; i < FFT_SIZE; ++i) {
            int idx = (bufferIndex + i) % FFT_SIZE;
            fftBuf[i] = inputBuffer[idx] * window[i];
        }

        std::vector<float> real(FFT_SIZE, 0), imag(FFT_SIZE, 0);
        
        // Прямое FFT (с использованием таблиц)
        for (int k = 0; k < FFT_SIZE; ++k) {
            for (int n = 0; n < FFT_SIZE; ++n) {
                real[k] += fftBuf[n] * dft_cos[k][n];
                imag[k] += fftBuf[n] * dft_sin[k][n];
            }
            
            if (k <= FFT_SIZE / 2) {
                float mag = sqrtf(real[k]*real[k] + imag[k]*imag[k]);
                if (learning) {
                    noiseProfile[k] = std::max(noiseProfile[k], mag);
                } else if (reducing) {
                    float cleanMag = std::max(mag - noiseProfile[k] * 1.5f, mag * 0.05f); // Агрессивное вычитание
                    float phase = atan2f(imag[k], real[k]);
                    real[k] = cleanMag * cosf(phase);
                    imag[k] = cleanMag * sinf(phase);
                }
            }
        }

        // Обратное FFT
        for (int n = 0; n < FFT_SIZE; ++n) {
            float val = 0;
            for (int k = 0; k < FFT_SIZE; ++k) {
                // Используем симметрию
                float cosVal = dft_cos[k][n]; 
                float sinVal = -dft_sin[k][n]; // Инвертируем знак для обратного
                val += real[k] * cosVal + imag[k] * sinVal;
            }
            val /= FFT_SIZE;
            
            // Применяем окно и складываем в буфер перекрытия (Overlap-Add)
            int outIdx = (bufferIndex + n) % FFT_SIZE;
            overlapBuffer[outIdx] += val * window[n];
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
    return env->NewStringUTF("DSP Engine: Ready. Fast Noise Reduction.");
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
