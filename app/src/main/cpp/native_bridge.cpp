// Ponte JNI entre o app Kotlin e o núcleo do emulador.
// O núcleo real (ex.: PCSX2 como libretro) é carregado via dlopen se existir
// como libpcsx2_libretro.so na pasta de libs nativas do app.
#include <jni.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <string>
#include <atomic>

#define TAG "PS2Bridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

static void* g_core = nullptr;
static ANativeWindow* g_window = nullptr;
static std::atomic<unsigned> g_buttons{0};
static std::atomic<bool> g_running{false};

extern "C" {

JNIEXPORT jstring JNICALL
Java_com_exemplo_ps2console_NativeBridge_nativeInit(JNIEnv* env, jobject, jstring libDir) {
    const char* dir = env->GetStringUTFChars(libDir, nullptr);
    std::string path = std::string(dir) + "/libpcsx2_libretro.so";
    env->ReleaseStringUTFChars(libDir, dir);
    g_core = dlopen(path.c_str(), RTLD_NOW);
    if (!g_core) return env->NewStringUTF("Núcleo PS2 não instalado (libpcsx2_libretro.so ausente).");
    return env->NewStringUTF("Núcleo PS2 carregado.");
}

JNIEXPORT jboolean JNICALL
Java_com_exemplo_ps2console_NativeBridge_nativeValidateBios(JNIEnv* env, jobject, jstring p) {
    const char* path = env->GetStringUTFChars(p, nullptr);
    struct stat st{};
    bool ok = stat(path, &st) == 0 && st.st_size >= 512 * 1024 && st.st_size <= 8 * 1024 * 1024;
    env->ReleaseStringUTFChars(p, path);
    return ok;
}

// ISO9660: "CD001" no offset 0x8001 (setor 16).
JNIEXPORT jboolean JNICALL
Java_com_exemplo_ps2console_NativeBridge_nativeValidateIso(JNIEnv*, jobject, jint fd) {
    char buf[5] = {0};
    if (pread(fd, buf, 5, 0x8001) != 5) return false;
    return std::string(buf, 5) == "CD001";
}

JNIEXPORT jstring JNICALL
Java_com_exemplo_ps2console_NativeBridge_nativeStart(JNIEnv* env, jobject, jstring bios, jint isoFd, jobject surface) {
    if (!g_core) return env->NewStringUTF("Sem núcleo: instale libpcsx2_libretro.so.");
    g_window = ANativeWindow_fromSurface(env, surface);
    g_running = true;
    LOGI("start: isoFd=%d (TODO: retro_load_game + loop de frames)", isoFd);
    // TODO: resolver retro_init/retro_load_game/retro_run com dlsym e rodar em thread.
    return env->NewStringUTF("Núcleo encontrado; integração libretro ainda pendente.");
}

JNIEXPORT void JNICALL
Java_com_exemplo_ps2console_NativeBridge_nativeSetButton(JNIEnv*, jobject, jint id, jboolean down) {
    if (id < 0 || id > 31) return;
    if (down) g_buttons |= (1u << id); else g_buttons &= ~(1u << id);
}

JNIEXPORT void JNICALL
Java_com_exemplo_ps2console_NativeBridge_nativeStop(JNIEnv*, jobject) {
    g_running = false;
    if (g_window) { ANativeWindow_release(g_window); g_window = nullptr; }
}

}
