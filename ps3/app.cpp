// The host side on the PS3: settings at their defaults, the port's folder (ps3/main.cpp) as the exe folder, no window
#include <cstdlib>
#include <filesystem>
#include <mutex>

#include "app/app.h"
#include "ps3/screen.h"

namespace app {

Settings& Get() {
    static Settings s;
    return s;
}
void Load() {}
void Save() {}
void ApplyDisplay() {}
Mode Desktop() { return {1280, 720}; }
Mode Output() { return Desktop(); }
float RenderScale(uint32_t) { return 1.0f; }
bool Widescreen() { return false; }
bool WideClose() { return false; }

std::filesystem::path ExeDir() { return ps3::g_dir; }
int Instance() { return 0; }
void* Window() { return nullptr; }

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
