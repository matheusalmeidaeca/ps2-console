// Ponte JNI + host libretro com contexto EGL (OpenGL ES 3).
// Tudo (init, load, run) acontece numa única thread, dona do contexto EGL.
// Registra um log em <filesDir>/system.log (visível no botão "Ver log" do app).
#include <jni.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <thread>

#define TAG "PS2Bridge"

// ---- tipos mínimos do libretro ----
struct retro_game_info { const char* path; const void* data; size_t size; const char* meta; };
struct retro_system_info { const char* library_name; const char* library_version; const char* valid_extensions; bool need_fullpath; bool block_extract; };
struct retro_game_geometry { unsigned base_width, base_height, max_width, max_height; float aspect_ratio; };
struct retro_system_timing { double fps; double sample_rate; };
struct retro_system_av_info { retro_game_geometry geometry; retro_system_timing timing; };
struct retro_variable { const char* key; const char* value; };
typedef void (*retro_proc)(void);
typedef void (*hw_reset_t)(void);
typedef uintptr_t (*hw_fbo_t)(void);
typedef retro_proc (*hw_proc_t)(const char*);
struct retro_hw_render_callback {
    unsigned context_type;
    hw_reset_t context_reset;
    hw_fbo_t get_current_framebuffer;
    hw_proc_t get_proc_address;
    bool depth, stencil, bottom_left_origin;
    unsigned version_major, version_minor;
    bool cache_context;
    hw_reset_t context_destroy;
    bool debug_context;
};
struct retro_log_callback { void (*log)(unsigned, const char*, ...); };

typedef bool (*env_cb)(unsigned, void*);
typedef void (*video_cb)(const void*, unsigned, unsigned, size_t);
typedef void (*audio_cb)(int16_t, int16_t);
typedef size_t (*audio_batch_cb)(const int16_t*, size_t);
typedef void (*poll_cb)();
typedef int16_t (*input_cb)(unsigned, unsigned, unsigned, unsigned);

enum {
    ENV_GET_SYSTEM_DIR = 9, ENV_SET_PIXEL_FORMAT = 10, ENV_SET_HW_RENDER = 14,
    ENV_GET_VARIABLE = 15, ENV_SET_VARIABLES = 16, ENV_GET_VARIABLE_UPDATE = 17,
    ENV_GET_LOG_INTERFACE = 27, ENV_GET_SAVE_DIR = 31, ENV_GET_PREFERRED_HW_RENDER = 56,
};
enum { PF_0RGB1555 = 0, PF_XRGB8888 = 1, PF_RGB565 = 2 };
enum { CT_NONE = 0, CT_OPENGL = 1, CT_GLES2 = 2, CT_OPENGL_CORE = 3, CT_GLES3 = 4, CT_GLES_VERSION = 5, CT_VULKAN = 6 };

// ---- estado ----
static void* g_core = nullptr;
static ANativeWindow* g_window = nullptr;
static std::atomic<unsigned> g_buttons{0};
static std::atomic<bool> g_running{false};
static std::thread g_thread;
static int g_pixfmt = PF_0RGB1555;
static std::string g_systemDir, g_logPath;
static std::map<std::string, std::string> g_opts;  // overrides (app/heurística)
static std::map<std::string, std::string> g_vars;  // padrões vindos do núcleo
static retro_hw_render_callback g_hw{};
static bool g_hwActive = false;
static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLSurface g_surf = EGL_NO_SURFACE;
static EGLContext g_ctx = EGL_NO_CONTEXT;

static void fileLog(const char* fmt, ...) {
    char buf[1024];
    va_list a; va_start(a, fmt); vsnprintf(buf, sizeof buf, fmt, a); va_end(a);
    __android_log_print(ANDROID_LOG_INFO, TAG, "%s", buf);
    if (g_logPath.empty()) return;
    if (FILE* f = fopen(g_logPath.c_str(), "a")) {
        fputs(buf, f);
        size_t n = strlen(buf);
        if (n == 0 || buf[n - 1] != '\n') fputc('\n', f);
        fclose(f);
    }
}
static void coreLog(unsigned level, const char* fmt, ...) {
    char buf[1024];
    va_list a; va_start(a, fmt); vsnprintf(buf, sizeof buf, fmt, a); va_end(a);
    fileLog("[core %u] %s", level, buf);
}

static void (*p_set_env)(env_cb) = nullptr;
static void (*p_set_video)(video_cb) = nullptr;
static void (*p_set_audio)(audio_cb) = nullptr;
static void (*p_set_audio_batch)(audio_batch_cb) = nullptr;
static void (*p_set_poll)(poll_cb) = nullptr;
static void (*p_set_input)(input_cb) = nullptr;
static void (*p_init)() = nullptr;
static void (*p_deinit)() = nullptr;
static void (*p_get_avinfo)(retro_system_av_info*) = nullptr;
static bool (*p_load_game)(const retro_game_info*) = nullptr;
static void (*p_unload_game)() = nullptr;
static void (*p_run)() = nullptr;

static bool loadSyms() {
    auto L = [](const char* n) { return dlsym(g_core, n); };
    p_set_env = (decltype(p_set_env))L("retro_set_environment");
    p_set_video = (decltype(p_set_video))L("retro_set_video_refresh");
    p_set_audio = (decltype(p_set_audio))L("retro_set_audio_sample");
    p_set_audio_batch = (decltype(p_set_audio_batch))L("retro_set_audio_sample_batch");
    p_set_poll = (decltype(p_set_poll))L("retro_set_input_poll");
    p_set_input = (decltype(p_set_input))L("retro_set_input_state");
    p_init = (decltype(p_init))L("retro_init");
    p_deinit = (decltype(p_deinit))L("retro_deinit");
    p_get_avinfo = (decltype(p_get_avinfo))L("retro_get_system_av_info");
    p_load_game = (decltype(p_load_game))L("retro_load_game");
    p_unload_game = (decltype(p_unload_game))L("retro_unload_game");
    p_run = (decltype(p_run))L("retro_run");
    return p_set_env && p_set_video && p_set_audio && p_set_audio_batch && p_set_poll &&
           p_set_input && p_init && p_deinit && p_get_avinfo && p_load_game && p_unload_game && p_run;
}

// ---- EGL ----
static bool initEgl() {
    g_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_dpy == EGL_NO_DISPLAY || !eglInitialize(g_dpy, nullptr, nullptr)) { fileLog("EGL: sem display"); return false; }
    const EGLint ca[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                         EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                         EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE};
    EGLConfig cfg; EGLint n = 0;
    if (!eglChooseConfig(g_dpy, ca, &cfg, 1, &n) || n < 1) { fileLog("EGL: sem config ES3"); return false; }
    g_surf = eglCreateWindowSurface(g_dpy, cfg, g_window, nullptr);
    const EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    g_ctx = eglCreateContext(g_dpy, cfg, EGL_NO_CONTEXT, xa);
    if (g_surf == EGL_NO_SURFACE || g_ctx == EGL_NO_CONTEXT || !eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx)) {
        fileLog("EGL: falha ao criar/ativar contexto (erro 0x%x)", eglGetError()); return false;
    }
    fileLog("EGL ok.");
    return true;
}
static void shutdownEgl() {
    if (g_dpy == EGL_NO_DISPLAY) return;
    eglMakeCurrent(g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (g_ctx != EGL_NO_CONTEXT) eglDestroyContext(g_dpy, g_ctx);
    if (g_surf != EGL_NO_SURFACE) eglDestroySurface(g_dpy, g_surf);
    eglTerminate(g_dpy);
    g_dpy = EGL_NO_DISPLAY; g_ctx = EGL_NO_CONTEXT; g_surf = EGL_NO_SURFACE;
}
static uintptr_t hwFbo() { return 0; }
static retro_proc hwProc(const char* s) { return (retro_proc)eglGetProcAddress(s); }

static std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

// "Descrição; a|b|c" -> padrão = a. Se a chave for de renderizador, prefere opção OpenGL.
static void parseVariables(const retro_variable* v) {
    for (; v && v->key; v++) {
        std::string val = v->value ? v->value : "";
        fileLog("opção: %s = %s", v->key, val.c_str());
        size_t p = val.find("; ");
        std::string opts = p == std::string::npos ? val : val.substr(p + 2);
        std::string first = opts.substr(0, opts.find('|'));
        g_vars[v->key] = first;
        if (lower(v->key).find("renderer") != std::string::npos && !g_opts.count(v->key)) {
            size_t s = 0;
            while (s <= opts.size()) {
                size_t e = opts.find('|', s); if (e == std::string::npos) e = opts.size();
                std::string o = opts.substr(s, e - s), lo = lower(o);
                if (lo.find("opengl") != std::string::npos || lo.find("gles") != std::string::npos) { g_opts[v->key] = o; break; }
                s = e + 1;
            }
        }
    }
}

// ---- callbacks ----
static bool onEnv(unsigned cmd, void* data) {
    switch (cmd) {
        case ENV_GET_SYSTEM_DIR:
        case ENV_GET_SAVE_DIR: *(const char**)data = g_systemDir.c_str(); return true;
        case ENV_SET_PIXEL_FORMAT: g_pixfmt = *(int*)data; return true;
        case ENV_GET_VARIABLE_UPDATE: *(bool*)data = false; return true;
        case ENV_GET_LOG_INTERFACE: ((retro_log_callback*)data)->log = coreLog; return true;
        case ENV_SET_VARIABLES: parseVariables((const retro_variable*)data); return true;
        case ENV_GET_VARIABLE: {
            auto* v = (retro_variable*)data;
            auto o = g_opts.find(v->key); if (o != g_opts.end()) { v->value = o->second.c_str(); return true; }
            auto d = g_vars.find(v->key); if (d != g_vars.end()) { v->value = d->second.c_str(); return true; }
            return false;
        }
        case ENV_GET_PREFERRED_HW_RENDER: *(unsigned*)data = CT_GLES3; return true;
        case ENV_SET_HW_RENDER: {
            auto* hw = (retro_hw_render_callback*)data;
            fileLog("núcleo pediu render tipo %u (v%u.%u)", hw->context_type, hw->version_major, hw->version_minor);
            if (hw->context_type != CT_GLES2 && hw->context_type != CT_GLES3 &&
                hw->context_type != CT_GLES_VERSION && hw->context_type != CT_OPENGL_CORE) {
                fileLog("tipo de render não suportado (Vulkan ainda não)."); return false;
            }
            if (!initEgl()) return false;
            hw->get_current_framebuffer = hwFbo;
            hw->get_proc_address = hwProc;
            g_hw = *hw; g_hwActive = true;
            return true;
        }
        default: return false;
    }
}

static void onVideo(const void* data, unsigned w, unsigned h, size_t pitch) {
    if (g_hwActive) {
        if (data == (const void*)-1) eglSwapBuffers(g_dpy, g_surf);  // RETRO_HW_FRAME_BUFFER_VALID
        return;
    }
    if (!data || !g_window) return;
    bool is565 = g_pixfmt == PF_RGB565;
    ANativeWindow_setBuffersGeometry(g_window, w, h, is565 ? WINDOW_FORMAT_RGB_565 : WINDOW_FORMAT_RGBA_8888);
    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(g_window, &buf, nullptr) != 0) return;
    for (unsigned y = 0; y < h && y < (unsigned)buf.height; y++) {
        const uint8_t* src = (const uint8_t*)data + y * pitch;
        if (is565) {
            memcpy((uint16_t*)buf.bits + y * buf.stride, src, w * 2);
        } else if (g_pixfmt == PF_XRGB8888) {
            uint32_t* dst = (uint32_t*)buf.bits + y * buf.stride;
            const uint32_t* s = (const uint32_t*)src;
            for (unsigned x = 0; x < w; x++) {
                uint32_t p = s[x];
                dst[x] = 0xFF000000u | ((p & 0xFF) << 16) | (p & 0xFF00) | ((p >> 16) & 0xFF);
            }
        }
    }
    ANativeWindow_unlockAndPost(g_window);
}

static void onAudio(int16_t, int16_t) {}
static size_t onAudioBatch(const int16_t*, size_t frames) { return frames; }
static void onPoll() {}

// ids do app: 0 ↑ 1 ↓ 2 ← 3 → 4 X 5 O 6 □ 7 △ 8 START 9 SELECT
static int16_t onInput(unsigned port, unsigned device, unsigned, unsigned id) {
    if (port != 0 || device != 1) return 0;
    int bit;
    switch (id) {
        case 4: bit = 0; break;  // UP
        case 5: bit = 1; break;  // DOWN
        case 6: bit = 2; break;  // LEFT
        case 7: bit = 3; break;  // RIGHT
        case 0: bit = 4; break;  // B  -> X
        case 8: bit = 5; break;  // A  -> O
        case 1: bit = 6; break;  // Y  -> quadrado
        case 9: bit = 7; break;  // X  -> triângulo
        case 3: bit = 8; break;  // START
        case 2: bit = 9; break;  // SELECT
        default: return 0;
    }
    return (g_buttons.load() >> bit) & 1;
}

static void joinThread() { if (g_thread.joinable()) g_thread.join(); }

extern "C" {

JNIEXPORT jstring JNICALL
Java_com_exemplo_ps2console_NativeBridge_nativeInit(JNIEnv* env, jobject, jstring libDir) {
    const char* dir = env->GetStringUTFChars(libDir, nullptr);
    std::string path = std::string(dir) + "/libpcsx2_libretro.so";
    env->ReleaseStringUTFChars(libDir, dir);
    g_core = dlopen(path.c_str(), RTLD_NOW);
    if (!g_core) return env->NewStringUTF("Núcleo PS2 não instalado (libpcsx2_libretro.so ausente).");
    if (!loadSyms()) return env->NewStringUTF("Núcleo carregado, mas não é um núcleo libretro válido.");
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

JNIEXPORT jboolean JNICALL
Java_com_exemplo_ps2console_NativeBridge_nativeValidateIso(JNIEnv*, jobject, jint fd) {
    char buf[5] = {0};
    if (pread(fd, buf, 5, 0x8001) != 5) return false;
    return std::string(buf, 5) == "CD001";
}

JNIEXPORT void JNICALL
Java_com_exemplo_ps2console_NativeBridge_nativeSetOption(JNIEnv* env, jobject, jstring k, jstring v) {
    const char* ks = env->GetStringUTFChars(k, nullptr);
    const char* vs = env->GetStringUTFChars(v, nullptr);
    g_opts[ks] = vs;
    env->ReleaseStringUTFChars(k, ks);
    env->ReleaseStringUTFChars(v, vs);
}

JNIEXPORT jstring JNICALL
Java_com_exemplo_ps2console_NativeBridge_nativeStart(JNIEnv* env, jobject, jstring systemDir, jint isoFd, jobject surface) {
    if (!g_core || !p_run) return env->NewStringUTF("Sem núcleo: instale libpcsx2_libretro.so.");
    if (g_running) return env->NewStringUTF("Já está rodando.");
    joinThread();
    const char* sd = env->GetStringUTFChars(systemDir, nullptr);
    g_systemDir = sd;
    env->ReleaseStringUTFChars(systemDir, sd);
    g_logPath = g_systemDir + ".log";
    if (FILE* f = fopen(g_logPath.c_str(), "w")) fclose(f);
    g_window = ANativeWindow_fromSurface(env, surface);
    g_hwActive = false;
    g_vars.clear();

    auto prom = std::make_shared<std::promise<std::string>>();
    auto fut = prom->get_future();
    g_running = true;
    g_thread = std::thread([prom, isoFd]() {
        p_set_env(onEnv);
        p_set_video(onVideo); p_set_audio(onAudio); p_set_audio_batch(onAudioBatch);
        p_set_poll(onPoll); p_set_input(onInput);
        p_init();
        std::string isoPath = "/proc/self/fd/" + std::to_string(isoFd);
        retro_game_info gi{isoPath.c_str(), nullptr, 0, nullptr};
        bool ok = p_load_game(&gi);
        if (ok && g_hwActive && g_hw.context_reset) g_hw.context_reset();
        if (!ok) {
            fileLog("retro_load_game falhou.");
            p_deinit(); shutdownEgl();
            if (g_window) { ANativeWindow_release(g_window); g_window = nullptr; }
            g_hwActive = false; g_running = false;
            prom->set_value("O núcleo recusou o jogo. Veja o log.");
            return;
        }
        retro_system_av_info av{};
        p_get_avinfo(&av);
        double fps = av.timing.fps > 1 ? av.timing.fps : 60.0;
        fileLog("rodando a %.2f fps", fps);
        prom->set_value(g_hwActive ? "Rodando (OpenGL ES)." : "Rodando (software).");
        auto frame = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1.0 / fps));
        auto next = std::chrono::steady_clock::now();
        while (g_running) {
            p_run();
            next += frame;
            std::this_thread::sleep_until(next);
        }
        if (g_hwActive && g_hw.context_destroy) g_hw.context_destroy();
        p_unload_game(); p_deinit();
        shutdownEgl();
        if (g_window) { ANativeWindow_release(g_window); g_window = nullptr; }
        g_hwActive = false;
    });
    return env->NewStringUTF(fut.get().c_str());
}

JNIEXPORT void JNICALL
Java_com_exemplo_ps2console_NativeBridge_nativeSetButton(JNIEnv*, jobject, jint id, jboolean down) {
    if (id < 0 || id > 31) return;
    if (down) g_buttons |= (1u << id); else g_buttons &= ~(1u << id);
}

JNIEXPORT void JNICALL
Java_com_exemplo_ps2console_NativeBridge_nativeStop(JNIEnv*, jobject) {
    g_running = false;
    joinThread();
}

}
