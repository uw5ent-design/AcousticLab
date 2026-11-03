#include <jni.h>
#include <string>
#include <android/log.h>
#include <oboe/Oboe.h>
#include <random>
#include <cmath>
#include <atomic>
#include <vector>
#include <fstream>

#define LOG_TAG "AcousticDSP"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define FFT_SIZE 512
#define HOP_SIZE 256
#define FFT_BANDS 64

class AudioEngine : public oboe::AudioStreamCallback {
private:
    std::shared_ptr<oboe::AudioStream> mOutputStream;
    std::shared_ptr<oboe::AudioStream> mInputStream;
    std::mt19937 gen;
    std::uniform_real_distribution<float> dis;
    
    float fftOutput[FFT_BANDS];
    std::atomic<float> noiseVolume_{0.5f}; 
    std::atomic<float> micVolume_{0.5f};   
    std::atomic<bool> filterEnabled_{true};
    
    std::atomic<bool> isLearning_{false};
    std::atomic<bool> isReducing_{false};
    std::atomic<float> scale_{1.0f};      
    std::atomic<float> threshold_{0.05f}; 
    std::vector<float> noiseProfile;
    int learnFramesCounter = 0;

    std::vector<float> inputBuffer;
    std::vector<float> outputBuffer;
    int bufferIndex = 0;
    float window[FFT_SIZE];

    std::atomic<bool> isRecording_{false};
    std::vector<float> recordedBuffer;

    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
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
        outputBuffer.resize(FFT_SIZE, 0.0f);
        
        for (int i = 0; i < FFT_SIZE; ++i) {
            window[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (FFT_SIZE - 1)));
        }
        initFilters();
    }

    void initFilters() { 
        float fs = 48000.0f;
        float fc = sqrt(300.0f * 3400.0f); float bw = 3400.0f - 300.0f; float Q = fc / bw;
        float w0 = 2.0f * M_PI * fc / fs; float alpha = sin(w0) / (2.0f * Q);
        float a0 = 1.0f + alpha;
        bandpass.b0 = alpha / a0; bandpass.b2 = -alpha / a0; bandpass.a1 = -2.0f * cos(w0) / a0; bandpass.a2 = (1.0f - alpha) / a0;
        float w0_50 = 2.0f * M_PI * 50.0f / fs; float alpha_50 = sin(w0_50) / 10.0f; float a0_50 = 1.0f + alpha_50;
        notch50.b0 = 1.0f / a0_50; notch50.b1 = -2.0f * cos(w0_50) / a0_50; notch50.b2 = 1.0f / a0_50;
        notch50.a1 = -2.0f * cos(w0_50) / a0_50; notch50.a2 = (1.0f - alpha_50) / a0_50;
    }

    void setNoiseVol(float vol) { noiseVolume_.store(vol); }
    void setMicVol(float vol) { micVolume_.store(vol); }
    void setFilterEnabled(bool enabled) { filterEnabled_.store(enabled); }
    void startLearning() { isLearning_.store(true); learnFramesCounter = 0; }
    void setReducing(bool enabled) { isReducing_.store(enabled); }
    void setScale(float scale) { scale_.store(scale); }
    void setThreshold(float th) { threshold_.store(th); }
    
    void startRecording() { recordedBuffer.clear(); isRecording_.store(true); }
    
    void stopRecording(std::string path) {
        isRecording_.store(false);
        std::ofstream outfile(path, std::ios::binary);
        outfile.write(reinterpret_cast<const char*>(recordedBuffer.data()), recordedBuffer.size() * sizeof(float));
        outfile.close();
    }

    bool start() {
        oboe::AudioStreamBuilder inB; inB.setDirection(oboe::Direction::Input)->setPerformanceMode(oboe::PerformanceMode::LowLatency)->setSharingMode(oboe::SharingMode::Shared)->setFormat(oboe::AudioFormat::Float)->setChannelCount(oboe::ChannelCount::Mono);
        if (inB.openStream(mInputStream) != oboe::Result::OK) return false;
        oboe::AudioStreamBuilder outB; outB.setDirection(oboe::Direction::Output)->setPerformanceMode(oboe::PerformanceMode::LowLatency)->setSharingMode(oboe::SharingMode::Exclusive)->setFormat(oboe::AudioFormat::Float)->setChannelCount(oboe::ChannelCount::Mono)->setDataCallback(this);
        if (outB.openStream(mOutputStream) != oboe::Result::OK) return false;
        mInputStream->requestStart(); mOutputStream->requestStart();
        return true;
    }

    void stop() { 
        if (mInputStream) { mInputStream->stop(); mInputStream->close(); mInputStream.reset(); }
        if (mOutputStream) { mOutputStream->stop(); mOutputStream->close(); mOutputStream.reset(); }
    }

    void getFftData(float* outArray) { for (int i = 0; i < FFT_BANDS; ++i) outArray[i] = fftOutput[i]; }

    oboe::DataCallbackResult onAudioReady(oboe::AudioStream *oboeStream, void *audioData, int32_t numFrames) override {
        auto *floatData = static_cast<float *>(audioData);
        float micData[1024]; int32_t framesRead = 0;
        if (mInputStream) {
            auto result = mInputStream->read(micData, numFrames, 0); 
            if (result) framesRead = result.value();
        }

        float nVol = noiseVolume_.load();
        float mVol = micVolume_.load();
        bool useFilters = filterEnabled_.load();
        bool reducing = isReducing_.load();
        bool learning = isLearning_.load();

        for (int i = 0; i < numFrames; ++i) {
            float mic = (i < framesRead) ? micData[i] : 0.0f;
            float noise = dis(gen) * 0.5f; 
            
            float mixed = (noise * nVol) + (mic * mVol); 

            if (useFilters) {
                mixed = notch50.process(mixed);
                mixed = bandpass.process(mixed);
            }

            // Всегда пишем в буфер (чтобы не было разрывов при включении)
            inputBuffer[FFT_SIZE - HOP_SIZE + bufferIndex] = mixed;
            
            // Если шумодав ВКЛЮЧЕН, берем звук из обработанного буфера.
            // Если ВЫКЛЮЧЕН - звук идет напрямую (чистый шум без жужжания).
            if (reducing || learning) {
                floatData[i] = outputBuffer[bufferIndex];
            } else {
                floatData[i] = mixed;
            }
            
            bufferIndex++;

            if (bufferIndex >= HOP_SIZE) {
                processSTFT(reducing, learning);
                if (learning) {
                    learnFramesCounter++;
                    if (learnFramesCounter > 50) isLearning_.store(false);
                }
                bufferIndex = 0;
                for(int j=0; j<FFT_SIZE-HOP_SIZE; j++) {
                    inputBuffer[j] = inputBuffer[j+HOP_SIZE];
                    outputBuffer[j] = outputBuffer[j+HOP_SIZE];
                }
                for(int j=FFT_SIZE-HOP_SIZE; j<FFT_SIZE; j++) outputBuffer[j] = 0.0f;
            }
        }

        if (isRecording_.load()) {
            recordedBuffer.insert(recordedBuffer.end(), floatData, floatData + numFrames);
        }

        updateVisualizer(floatData, numFrames);
        return oboe::DataCallbackResult::Continue;
    }

    void fft(std::vector<float>& real, std::vector<float>& imag, bool invert) {
        int n = real.size();
        for (int i = 1, j = 0; i < n; i++) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap(real[i], real[j]); std::swap(imag[i], imag[j]); }
        }
        for (int len = 2; len <= n; len <<= 1) {
            float ang = 2 * M_PI / len * (invert ? -1 : 1);
            float wlen_r = cos(ang), wlen_i = sin(ang);
            for (int i = 0; i < n; i += len) {
                float w_r = 1, w_i = 0;
                for (int j = 0; j < len / 2; j++) {
                    float u_r = real[i+j], u_i = imag[i+j];
                    float v_r = real[i+j+len/2] * w_r - imag[i+j+len/2] * w_i;
                    float v_i = real[i+j+len/2] * w_i + imag[i+j+len/2] * w_r;
                    real[i+j] = u_r + v_r; imag[i+j] = u_i + v_i;
                    real[i+j+len/2] = u_r - v_r; imag[i+j+len/2] = u_i - v_i;
                    float next_w_r = w_r * wlen_r - w_i * wlen_i;
                    w_i = w_r * wlen_i + w_i * wlen_r; w_r = next_w_r;
                }
            }
        }
        if (invert) for (int i = 0; i < n; i++) { real[i] /= n; imag[i] /= n; }
    }

    void processSTFT(bool reducing, bool learning) {
        std::vector<float> real(FFT_SIZE), imag(FFT_SIZE, 0.0f);
        for (int i = 0; i < FFT_SIZE; ++i) real[i] = inputBuffer[i] * window[i];
        
        fft(real, imag, false);

        float currentScale = scale_.load();
        float currentThreshold = threshold_.load();

        for (int k = 0; k <= FFT_SIZE / 2; ++k) {
            float mag = sqrtf(real[k]*real[k] + imag[k]*imag[k]);
            if (learning) {
                noiseProfile[k] = std::max(noiseProfile[k], mag);
            } else if (reducing) {
                float subtractValue = noiseProfile[k] * currentScale;
                
                // 10% Spectral Floor (убирает жужжание)
                float cleanMag = std::max(mag - subtractValue, mag * 0.1f);
                
                if (mag < currentThreshold) {
                    cleanMag = mag * 0.02f; 
                }
                
                float phase = atan2f(imag[k], real[k]);
                real[k] = cleanMag * cosf(phase);
                imag[k] = cleanMag * sinf(phase);
            }
        }
        
        for (int k = FFT_SIZE / 2 + 1; k < FFT_SIZE; ++k) {
            real[k] = real[FFT_SIZE - k];
            imag[k] = -imag[FFT_SIZE - k];
        }

        fft(real, imag, true);

        for (int i = 0; i < FFT_SIZE; ++i) {
            // Коэффициент 0.66 правильный для Hann окна с 50% перекрытием
            outputBuffer[i] += real[i] * window[i] * 0.66f; 
        }
    }

    void updateVisualizer(float* data, int numFrames) {
        int analyzeFrames = (numFrames < 256) ? numFrames : 256;
        for (int k = 0; k < FFT_BANDS; ++k) {
            float real = 0, imag = 0;
            float freq = (float)k / FFT_BANDS;
            for (int n = 0; n < analyzeFrames; ++n) {
                float angle = 2.0f * M_PI * freq * n;
                real += data[n] * cosf(angle); imag -= data[n] * sinf(angle);
            }
            float mag = sqrtf(real*real + imag*imag) / analyzeFrames;
            fftOutput[k] = (mag > 0) ? (20.0f * log10f(mag) + 100.0f) : 0.0f;
            if (fftOutput[k] < 0) fftOutput[k] = 0; if (fftOutput[k] > 100) fftOutput[k] = 100;
        }
    }
};

AudioEngine engine;

extern "C" JNIEXPORT jstring JNICALL Java_com_uw5ent_acousticlab_MainActivity_getDspStatus(JNIEnv* env, jobject) { return env->NewStringUTF("DSP Engine: Anti-Buzz Active."); }
extern "C" JNIEXPORT jboolean JNICALL Java_com_uw5ent_acousticlab_MainActivity_startAudio(JNIEnv* env, jobject) { return engine.start(); }
extern "C" JNIEXPORT void JNICALL Java_com_uw5ent_acousticlab_MainActivity_stopAudio(JNIEnv* env, jobject) { engine.stop(); }
extern "C" JNIEXPORT void JNICALL Java_com_uw5ent_acousticlab_MainActivity_getFftData(JNIEnv* env, jobject, jfloatArray outArray) { jfloat* buffer = env->GetFloatArrayElements(outArray, 0); engine.getFftData(buffer); env->ReleaseFloatArrayElements(outArray, buffer, 0); }
extern "C" JNIEXPORT void JNICALL Java_com_uw5ent_acousticlab_MainActivity_setNoiseVolume(JNIEnv* env, jobject, jfloat vol) { engine.setNoiseVol(vol); }
extern "C" JNIEXPORT void JNICALL Java_com_uw5ent_acousticlab_MainActivity_setMicVolume(JNIEnv* env, jobject, jfloat vol) { engine.setMicVol(vol); }
extern "C" JNIEXPORT void JNICALL Java_com_uw5ent_acousticlab_MainActivity_setFilterEnabled(JNIEnv* env, jobject, jboolean enabled) { engine.setFilterEnabled(enabled); }
extern "C" JNIEXPORT void JNICALL Java_com_uw5ent_acousticlab_MainActivity_startLearning(JNIEnv* env, jobject) { engine.startLearning(); }
extern "C" JNIEXPORT void JNICALL Java_com_uw5ent_acousticlab_MainActivity_setNoiseReduction(JNIEnv* env, jobject, jboolean enabled) { engine.setReducing(enabled); }
extern "C" JNIEXPORT void JNICALL Java_com_uw5ent_acousticlab_MainActivity_setScale(JNIEnv* env, jobject, jfloat scale) { engine.setScale(scale); }
extern "C" JNIEXPORT void JNICALL Java_com_uw5ent_acousticlab_MainActivity_setThreshold(JNIEnv* env, jobject, jfloat th) { engine.setThreshold(th); }
extern "C" JNIEXPORT void JNICALL Java_com_uw5ent_acousticlab_MainActivity_startRecording(JNIEnv* env, jobject) { engine.startRecording(); }
extern "C" JNIEXPORT void JNICALL Java_com_uw5ent_acousticlab_MainActivity_stopRecording(JNIEnv* env, jobject, jstring path) {
    const char* nativeString = env->GetStringUTFChars(path, 0);
    engine.stopRecording(nativeString);
    env->ReleaseStringUTFChars(path, nativeString);
}
