// The host side on Android: settings at their defaults, the app's folder as the exe folder (main chdirs there), the
// game's own 16:9 picture on a phone's wide screen.
#include <filesystem>
#include <mutex>

#include "android/src/droid.h"
#include "app/app.h"

namespace app {

Settings& Get() {
    static Settings s;
    return s;
}
void Load() {}
void Save() {}
void ApplyDisplay() {}
Mode Desktop() {
    uint32_t w = 1280, h = 720;
    if (droid::g_window) droid::ScreenSize(w, h);
    return {w, h};
}
Mode Output() { return Desktop(); }
float RenderScale(uint32_t) { return 1.0f; }
bool Widescreen() { return true; }
bool WideClose() { return false; }

std::filesystem::path ExeDir() {
    static const std::filesystem::path dir = droid::Folder();
    return dir;
}
int Instance() { return 0; }
void* Window() { return droid::g_window; }

bool OpenSplash() { return false; }
void SplashFirstFrame() {}
void CloseSplash() {}
void OpenGameWindow(bool) {}
bool StartHandover() { return false; }
bool HandingOver() { return false; }
void FinishHandover() {}

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

void Pace() {}

}  // namespace app
