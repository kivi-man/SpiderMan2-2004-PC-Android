// The Android layer's shared state: the window and its GL context (SDL3, OpenGL ES 3.2), the touch controls
#pragma once
#include <atomic>
#include <cstdint>
#include <string>

struct SDL_Window;

namespace droid {

extern SDL_Window* g_window;
extern std::atomic<bool> g_gl_free;  // the main thread let go of the context for the renderer to take
extern std::atomic<bool> g_paused;   // the app is in the background: no drawing to its surface
extern std::atomic<bool> g_touch_visible;  // the on-screen controls are drawn (off while a controller is used)

// the GL side, from the renderer's thread
bool GlTake();
void GlRelease();
void GlSwap();
void GlSwapInterval(uint32_t blanks);
void ScreenSize(uint32_t& w, uint32_t& h);

// controllers, from the main thread's events
void GamepadAdded(uint32_t id);
void GamepadRemoved(uint32_t id);
void BackPressed();  // the phone's back button: the game's Start (pause) for a moment

// touch controls: fed by the main thread's events, read by the pad poll, drawn after each frame
void TouchEvent(int type, uint64_t finger, float x, float y);  // x, y 0 to 1 across the window
void TouchLayout(uint32_t screen_w, uint32_t screen_h);
struct TouchPad {
    uint16_t buttons = 0;
    uint8_t analog[8] = {};
    float lx = 0, ly = 0, rx = 0, ry = 0;
    bool camera = false;  // a drag is turning the camera
};
TouchPad TouchState();
void DrawTouch(uint32_t screen_w, uint32_t screen_h);  // GL thread

// the app's own folder on the phone's storage: the game, saves, logs
const std::string& Folder();

}  // namespace droid
