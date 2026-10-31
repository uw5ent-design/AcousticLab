#include <jni.h>
#include <string>
#include <android/log.h>

#define LOG_TAG "AcousticDSP"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

extern "C" JNIEXPORT jstring JNICALL
Java_com_uw5ent_acousticlab_MainActivity_getDspStatus(
        JNIEnv* env,
        jobject /* this */) {
    std::string status = "DSP Engine: Ready. Target Latency: <20ms.";
    LOGI("Native DSP module initialized.");
    return env->NewStringUTF(status.c_str());
}
