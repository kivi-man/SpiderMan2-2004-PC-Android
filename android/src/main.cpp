// Android entry (SDL3): finds the game, opens the window and its OpenGL ES 3.2 context, boots the recompiled image and
// then pumps the window's events. Layout in the app's folder on the phone's storage
// (Android/data/com.spiderman2.xbox/files, reachable over USB):
//   game/       the player's own copy: a disc image (.iso .xiso .cci .cso) or default.xbe with its data folder
//   save/       the game's saves (progress) and its own data on the Xbox's hard disk
//   cache/      the rest of the Xbox's hard disk and the shader list: safe to delete
//   logs/       sm2_NNN.log
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <EGL/egl.h>
#include <dlfcn.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "android/src/droid.h"
#include "app/app.h"
#include "core/input.h"
#include "core/log.h"
#include "gl/gl_api.h"
#include "gpu/backend.h"
#include "image_symbols.h"
#include "kernel/game_source.h"
#include "kernel/kernel.h"

namespace fs = std::filesystem;

extern const char kImageSha1[];

namespace droid {

SDL_Window* g_window = nullptr;
std::atomic<bool> g_gl_free{false};
std::atomic<bool> g_paused{false};
std::atomic<bool> g_touch_visible{true};

namespace {
SDL_GLContext g_ctx = nullptr;
}

const std::string& Folder() {
    static const std::string dir = [] {
        const char* p = SDL_GetAndroidExternalStoragePath();
        if (!p || !*p) p = SDL_GetAndroidInternalStoragePath();
        return std::string(p ? p : ".");
    }();
    return dir;
}

bool GlTake() {
    while (!g_gl_free.load()) SDL_Delay(1);
    if (!SDL_GL_MakeCurrent(g_window, g_ctx)) {
        LOGE("android: the GL context did not become current on the renderer's thread: %s", SDL_GetError());
        return false;
    }
    // the renderer's functions, the first time (gl/gl_api.cpp puts the ES stand ins over them)
    static bool loaded = false;
    if (!loaded) {
        const char* missing = nullptr;
        // libGLESv3's own exports first: a name looked up through EGL may come from the OpenGL ES 1 library
        auto get = [](const char* name) -> void* {
            static void* lib = dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL);
            if (void* f = lib ? dlsym(lib, name) : nullptr) return f;
            return reinterpret_cast<void*>(eglGetProcAddress(name));
        };
        if (!glapi::Load(get, &missing)) {
            LOGE("android: the GPU has no %s", missing ? missing : "?");
            return false;
        }
        loaded = true;
        LOGI("android: %s, %s", reinterpret_cast<const char*>(glGetString(GL_RENDERER)), reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    }
    return true;
}
void GlRelease() { SDL_GL_MakeCurrent(g_window, nullptr); }
void GlSwap() {
    // in the background the surface is gone: the frame is dropped, the game keeps its pace
    if (g_paused.load()) {
        SDL_Delay(16);
        return;
    }
    SDL_GL_SwapWindow(g_window);
}
void GlSwapInterval(uint32_t blanks) { SDL_GL_SetSwapInterval(int(blanks)); }
void ScreenSize(uint32_t& w, uint32_t& h) {
    int pw = 1280, ph = 720;
    SDL_GetWindowSizeInPixels(g_window, &pw, &ph);
    w = uint32_t(pw), h = uint32_t(ph);
}

}  // namespace droid

namespace {

[[noreturn]] void Fail(const char* title, const std::string& detail) {
    LOGE("android: %s: %s", title, detail.c_str());
    logx::Sync();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, detail.c_str(), droid::g_window);
    SDL_Quit();
    std::exit(1);
}

// the phone's language as the Xbox's code: 1 English, 3 German, 4 French, 5 Spanish, 6 Italian
uint32_t PhoneLanguage() {
    int n = 0;
    SDL_Locale** locales = SDL_GetPreferredLocales(&n);
    uint32_t code = 1;
    if (locales && n > 0 && locales[0]->language) {
        std::string l = locales[0]->language;
        code = l == "de" ? 3 : l == "fr" ? 4 : l == "es" ? 5 : l == "it" ? 6 : 1;
    }
    SDL_free(locales);
    return code;
}

}  // namespace

int main(int, char**) {
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    const std::string dir = droid::Folder();
    fs::create_directories(dir + "/game");
    chdir(dir.c_str());
    logx::Init((app::ExeDir() / "logs").string(), app::kGame.log);
    LOGI("android: folder %s, built for %s SHA-1 %s", dir.c_str(), IMAGE_XBE, kImageSha1);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    droid::g_window = SDL_CreateWindow("Spider-Man 2", 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!droid::g_window) Fail("No window", SDL_GetError());
    droid::g_ctx = SDL_GL_CreateContext(droid::g_window);
    if (!droid::g_ctx) {  // 3.1 with the renderer's stand ins (gl/gl_api.cpp)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        droid::g_ctx = SDL_GL_CreateContext(droid::g_window);
    }
    if (!droid::g_ctx) Fail("OpenGL ES 3.1 is needed", std::string("This phone's GPU did not give an OpenGL ES 3.1 context: ") + SDL_GetError());
    SDL_GL_SetSwapInterval(1);
    // the renderer takes the context on its own thread
    SDL_GL_MakeCurrent(droid::g_window, nullptr);
    droid::g_gl_free = true;

    input::Init();
    kernel::g_language = PhoneLanguage();
    LOGI("android: language %u", kernel::g_language);

    const std::string game = dir + "/game";
    setenv("GAME_DIR", game.c_str(), 1);
    kernel::GameSearch search = kernel::FindGameCopies({game, dir}, IMAGE_XBE, kImageSha1, "", {});
    if (search.copies.empty())
        Fail("Game not found", "Put your own copy of Spider-Man 2 (original Xbox) in\n\n" + game +
                                   "\n\nas a disc image (.iso, .xiso, .cci, .cso) or as default.xbe with its data folder.");
    kernel::GameCopy& copy = search.copies[0];
    if (!copy.same_release)
        Fail("Another release of the game", copy.name + " is not the release this build was made from (default.xbe SHA-1 " +
                                                 kImageSha1 + ", this one's " + copy.sha1 + ").");
    LOGI("android: the game from %s (%s)", copy.name.c_str(), copy.kind.c_str());
    fs::create_directories(dir + "/save/progress");
    fs::create_directories(dir + "/save/data");
    fs::create_directories(dir + "/cache");
    kernel::UseSaveFolders(copy.title_id, dir + "/save/progress", dir + "/save/data");
    if (copy.disc) kernel::MountDisc(std::move(copy.disc));
    if (app::kGame.init) app::kGame.init();
    gpu::g_render_thread = true;
    if (!kernel::Boot(std::move(copy.xbe), IMAGE_XBE, copy.folder.string(), dir + "/cache"))
        Fail("The game did not start", "See the newest file in " + dir + "/logs");

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_WaitEventTimeout(&e, 100)) {
            switch (e.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_TERMINATING: running = false; break;
                case SDL_EVENT_WILL_ENTER_BACKGROUND:
                case SDL_EVENT_DID_ENTER_BACKGROUND:
                    droid::g_paused = true;
                    kernel::FlushOpenFiles();
                    logx::Sync();
                    break;
                case SDL_EVENT_DID_ENTER_FOREGROUND: droid::g_paused = false; break;
                case SDL_EVENT_FINGER_DOWN:
                case SDL_EVENT_FINGER_MOTION:
                case SDL_EVENT_FINGER_UP:
                case SDL_EVENT_FINGER_CANCELED:
                    droid::TouchEvent(int(e.type), uint64_t(e.tfinger.fingerID), e.tfinger.x, e.tfinger.y);
                    break;
                case SDL_EVENT_GAMEPAD_ADDED: droid::GamepadAdded(e.gdevice.which); break;
                case SDL_EVENT_GAMEPAD_REMOVED: droid::GamepadRemoved(e.gdevice.which); break;
                case SDL_EVENT_KEY_DOWN:
                    if (e.key.key == SDLK_AC_BACK) droid::BackPressed();
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                    // a controller in use hides the on-screen controls until the screen is touched again
                    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.gaxis.value > 12000 || e.gaxis.value < -12000)
                        droid::g_touch_visible = false;
                    break;
                default: break;
            }
            if (!running) break;
        }
        logx::Flush();
    }
    kernel::FlushOpenFiles();
    logx::Sync();
    SDL_Quit();
    std::_Exit(0);
}
