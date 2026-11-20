// Keyboard and mouse for the Windows build: a virtual controller on port 1, merged with a real one.
// Keys press the pad's buttons and sticks; the mouse (raw input, no pointer acceleration) turns the camera through
// the right stick. While the mouse turns it, the game's camera object gets no stick threshold and no acceleration, so
// the camera follows the mouse 1:1; its own values come back when the mouse stops. Settings: [keyboard_mouse] and
// [keyboard_mouse.keys] in settings.toml.
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "common/camera_assist.h"
#include "common/kbm.h"
#include "core/input.h"
#include "core/log.h"

namespace kbm {

namespace {

// what a key does
enum Target {
    kUp, kDown, kLeft, kRight, kStart, kBack, kLThumb, kRThumb,  // the pad's button bits, in order
    kA, kB, kX, kY, kBlack, kWhite, kLTrigger, kRTrigger,        // analog[0..7]
    kMoveUp, kMoveDown, kMoveLeft, kMoveRight,                   // left stick
    kLookUp, kLookDown, kLookLeft, kLookRight,                   // right stick
    kTargets
};
const char* kTargetNames[kTargets] = {
    "dpad_up", "dpad_down", "dpad_left", "dpad_right", "start", "back", "left_thumb", "right_thumb",
    "a", "b", "x", "y", "black", "white", "left_trigger", "right_trigger",
    "move_up", "move_down", "move_left", "move_right", "look_up", "look_down", "look_left", "look_right"};
const char* kDefaultKeys[kTargets] = {
    "Up", "Down", "Left", "Right", "Esc, Enter", "Tab", "LCtrl", "Mouse3",
    "Space", "E", "Mouse1", "F", "R", "Q", "LShift", "Mouse2",
    "W", "S", "A", "D", "Num8", "Num2", "Num4", "Num6"};

struct Settings {
    bool enabled = true;
    double sensitivity = 0.25;  // stick per mouse speed: 1000 counts a second is a full stick at 1
    double camera_speed = 4.0;  // top turn speed while the mouse turns the camera
    bool mouse_camera = true;
    bool invert_x = false, invert_y = false;
    double stick_deadzone = 0.214;  // the game ignores the right stick below this
    double smoothing_ms = 15;
    std::string toggle = "F10";
    std::string keys[kTargets];
    Settings() { std::copy(std::begin(kDefaultKeys), std::end(kDefaultKeys), keys); }
};
Settings g_set;
std::vector<int> g_binds[kTargets];  // virtual keys per target
int g_toggle_vk = VK_F10;

std::atomic<HWND> g_hwnd{};
std::atomic<bool> g_capture{true};  // the player wants the mouse captured (F10 toggles)
std::atomic<bool> g_captured{};     // it is: clipped and hidden
std::atomic<bool> g_suppress{};     // the click that took the mouse back is not a game press
std::atomic<int> g_raw_x{}, g_raw_y{}, g_wheel{};

int VirtualKey(std::string name) {
    name.erase(std::remove_if(name.begin(), name.end(), ::isspace), name.end());
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    if (name.size() == 1 && std::isalnum((unsigned char)name[0])) return std::toupper(name[0]);
    if (name.size() >= 2 && name[0] == 'f' && std::isdigit((unsigned char)name[1])) {
        int n = std::atoi(name.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    if (name.rfind("num", 0) == 0 && name.size() == 4 && std::isdigit((unsigned char)name[3])) return VK_NUMPAD0 + name[3] - '0';
    static const std::map<std::string, int> kNames = {
        {"space", VK_SPACE}, {"enter", VK_RETURN}, {"esc", VK_ESCAPE}, {"escape", VK_ESCAPE}, {"tab", VK_TAB},
        {"backspace", VK_BACK}, {"capslock", VK_CAPITAL}, {"shift", VK_SHIFT}, {"lshift", VK_LSHIFT},
        {"rshift", VK_RSHIFT}, {"ctrl", VK_CONTROL}, {"lctrl", VK_LCONTROL}, {"rctrl", VK_RCONTROL}, {"alt", VK_MENU},
        {"lalt", VK_LMENU}, {"ralt", VK_RMENU}, {"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT},
        {"right", VK_RIGHT}, {"mouse1", VK_LBUTTON}, {"mouse2", VK_RBUTTON}, {"mouse3", VK_MBUTTON},
        {"mouse4", VK_XBUTTON1}, {"mouse5", VK_XBUTTON2}};
    auto it = kNames.find(name);
    return it == kNames.end() ? 0 : it->second;
}

bool IsMouseKey(int vk) {
    return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
}

void Resolve() {
    for (int t = 0; t < kTargets; t++) {
        g_binds[t].clear();
        std::string list = g_set.keys[t];
        size_t start = 0;
        while (start <= list.size()) {
            size_t end = list.find(',', start);
            if (end == std::string::npos) end = list.size();
            std::string name = list.substr(start, end - start);
            if (int vk = VirtualKey(name)) g_binds[t].push_back(vk);
            else if (name.find_first_not_of(" \t") != std::string::npos) LOGW("kbm: unknown key '%s' for %s", name.c_str(), kTargetNames[t]);
            start = end + 1;
        }
    }
    g_toggle_vk = VirtualKey(g_set.toggle);
}

bool Down(int vk) { return GetAsyncKeyState(vk) & 0x8000; }

bool Focused() {
    HWND h = g_hwnd.load();
    return h && GetForegroundWindow() == h && !IsIconic(h);
}

void Release() {
    if (g_captured.exchange(false)) {
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {}
    }
}

void Capture() {
    HWND h = g_hwnd.load();
    RECT rc;
    if (!h || !GetClientRect(h, &rc)) return;
    POINT tl{rc.left, rc.top}, br{rc.right, rc.bottom};
    ClientToScreen(h, &tl);
    ClientToScreen(h, &br);
    RECT clip{tl.x, tl.y, br.x, br.y};
    ClipCursor(&clip);
    if (!g_captured.exchange(true)) {
        SetCursorPos((tl.x + br.x) / 2, (tl.y + br.y) / 2);
        while (ShowCursor(FALSE) >= 0) {}
    }
}

// ------------------------------------------------------------------------------------------------ the pad
std::mutex g_lock;
using Clock = std::chrono::steady_clock;
Clock::time_point g_last = Clock::now(), g_last_move = Clock::now() - std::chrono::seconds(1);
float g_look_x = 0, g_look_y = 0;
bool g_toggle_was = false;
// the wheel taps the d-pad, a notch a tap: 70 ms down, 70 ms up
Clock::time_point g_wheel_until{};
uint16_t g_wheel_button = 0;

int16_t Stick(float v) { return int16_t(std::clamp(v, -1.0f, 1.0f) * 32767.0f); }

void Normalize(float& x, float& y) {
    float len = std::sqrt(x * x + y * y);
    if (len > 1) x /= len, y /= len;
}

}  // namespace

void Load(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    if (const toml::table* s = t["keyboard_mouse"].as_table()) {
        const toml::table& k = *s;
        g_set.enabled = k["enabled"].value_or(g_set.enabled);
        g_set.sensitivity = k["sensitivity"].value_or(g_set.sensitivity);
        g_set.camera_speed = k["camera_speed"].value_or(g_set.camera_speed);
        g_set.mouse_camera = k["mouse_camera"].value_or(g_set.mouse_camera);
        g_set.invert_x = k["invert_x"].value_or(g_set.invert_x);
        g_set.invert_y = k["invert_y"].value_or(g_set.invert_y);
        g_set.stick_deadzone = std::clamp(k["stick_deadzone"].value_or(g_set.stick_deadzone), 0.0, 0.9);
        g_set.smoothing_ms = std::max(k["smoothing_ms"].value_or(g_set.smoothing_ms), 0.0);
        g_set.toggle = k["capture_toggle"].value_or(g_set.toggle);
        if (const toml::table* keys = k["keys"].as_table())
            for (int i = 0; i < kTargets; i++) g_set.keys[i] = (*keys)[kTargetNames[i]].value_or(g_set.keys[i]);
    }
    Resolve();
}

void Save(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    toml::table keys;
    for (int i = 0; i < kTargets; i++) keys.insert(kTargetNames[i], g_set.keys[i]);
    toml::table k{{"enabled", g_set.enabled},
                  {"sensitivity", g_set.sensitivity},
                  {"camera_speed", g_set.camera_speed},
                  {"mouse_camera", g_set.mouse_camera},
                  {"invert_x", g_set.invert_x},
                  {"invert_y", g_set.invert_y},
                  {"stick_deadzone", g_set.stick_deadzone},
                  {"smoothing_ms", g_set.smoothing_ms},
                  {"capture_toggle", g_set.toggle}};
    k.insert("keys", std::move(keys));
    t.insert_or_assign("keyboard_mouse", std::move(k));
}

void Window(void* hwnd) {
    if (g_binds[kA].empty() && g_binds[kStart].empty()) Resolve();  // no settings file yet
    g_hwnd = HWND(hwnd);
    if (!g_set.enabled) return;
    RAWINPUTDEVICE rid{0x01, 0x02, 0, HWND(hwnd)};  // generic desktop, mouse
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) LOGW("kbm: raw mouse input unavailable (%lu)", GetLastError());
    LOGI("kbm: keyboard and mouse on controller 1, %s frees the mouse", g_set.toggle.c_str());
}

bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    if (!g_set.enabled || HWND(hwnd) != g_hwnd.load()) return false;
    switch (msg) {
        case WM_INPUT: {
            RAWINPUT ri;
            UINT size = sizeof(ri);
            if (GetRawInputData(HRAWINPUT(lp), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != UINT(-1) &&
                ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) && g_captured) {
                g_raw_x += ri.data.mouse.lLastX;
                g_raw_y += ri.data.mouse.lLastY;
            }
            return false;
        }
        case WM_MOUSEWHEEL:
            if (g_captured) g_wheel += GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            return false;
        case WM_LBUTTONDOWN:
            if (!g_capture) {
                g_capture = true;
                g_suppress = true;  // until every mouse button is up
            }
            return false;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) Release();
            return false;
        case WM_KILLFOCUS:
            Release();
            return false;
        case WM_SETCURSOR:
            if (g_captured && LOWORD(lp) == HTCLIENT) {
                SetCursor(nullptr);
                *result = TRUE;
                return true;
            }
            return false;
    }
    return false;
}

void Pad(int port, input::Pad& pad) {
    if (port != 0 || !g_set.enabled) return;
    std::lock_guard lock(g_lock);
    auto now = Clock::now();
    float dt = std::max(std::chrono::duration<float>(now - g_last).count(), 0.0005f);
    g_last = now;
    bool focused = Focused();

    bool toggle = focused && g_toggle_vk && Down(g_toggle_vk);
    if (toggle && !g_toggle_was) {
        g_capture = !g_capture;
        LOGI("kbm: mouse %s", g_capture ? "captured" : "freed");
    }
    g_toggle_was = toggle;
    if (focused && g_capture) Capture();
    else Release();

    bool any_mouse = Down(VK_LBUTTON) || Down(VK_RBUTTON) || Down(VK_MBUTTON) || Down(VK_XBUTTON1) || Down(VK_XBUTTON2);
    if (!any_mouse) g_suppress = false;
    bool mouse_live = g_captured && !g_suppress;

    float dx = float(g_raw_x.exchange(0)), dy = float(g_raw_y.exchange(0));
    if (!g_captured) dx = dy = 0;
    if (dx || dy) g_last_move = now;
    // mouse speed into a stick; the game turns the camera against the stick on both axes, so mouse right is the
    // stick left and mouse up the stick down
    float scale = float(g_set.sensitivity) / 1000.0f;
    float vx = (g_set.invert_x ? 1.0f : -1.0f) * dx / dt * scale;
    float vy = (g_set.invert_y ? -1.0f : 1.0f) * dy / dt * scale;
    float alpha = g_set.smoothing_ms <= 0 ? 1.0f : 1.0f - std::exp(-dt * 1000.0f / float(g_set.smoothing_ms));
    g_look_x += (vx - g_look_x) * alpha;
    g_look_y += (vy - g_look_y) * alpha;

    if (g_set.mouse_camera) camera_assist::Update(focused && now - g_last_move < std::chrono::milliseconds(150), float(g_set.camera_speed));
    if (!focused) return;

    bool down[kTargets] = {};
    bool any = false;
    for (int t = 0; t < kTargets; t++)
        for (int vk : g_binds[t])
            if ((mouse_live || !IsMouseKey(vk)) && Down(vk)) down[t] = any = true;

    // the wheel: d-pad up or down, a tap a notch
    if (now >= g_wheel_until) {
        if (g_wheel_button) {
            g_wheel_button = 0;
            g_wheel_until = now + std::chrono::milliseconds(70);
        } else if (int n = g_wheel.load()) {
            g_wheel -= n > 0 ? 1 : -1;
            g_wheel_button = n > 0 ? 1 << kUp : 1 << kDown;
            g_wheel_until = now + std::chrono::milliseconds(70);
        }
    }

    for (int b = kUp; b <= kRThumb; b++)
        if (down[b]) pad.buttons |= uint16_t(1 << b);
    pad.buttons |= g_wheel_button;
    for (int a = kA; a <= kRTrigger; a++)
        if (down[a]) pad.analog[a - kA] = 0xFF;

    float mx = float(down[kMoveRight] - down[kMoveLeft]), my = float(down[kMoveUp] - down[kMoveDown]);
    Normalize(mx, my);
    if (mx || my) pad.lx = Stick(mx), pad.ly = Stick(my);

    float rx = g_look_x, ry = g_look_y;
    float mag = std::sqrt(rx * rx + ry * ry);
    if (mag > 0.002f) {
        float out = float(g_set.stick_deadzone) + (1.0f - float(g_set.stick_deadzone)) * std::min(mag, 1.0f);
        rx = rx / mag * out, ry = ry / mag * out;
    } else {
        rx = ry = 0;
    }
    float kx = float(down[kLookRight] - down[kLookLeft]), ky = float(down[kLookUp] - down[kLookDown]);
    if (kx || ky) {
        Normalize(kx, ky);
        rx = kx, ry = ky;
    }
    if (rx || ry) pad.rx = Stick(rx), pad.ry = Stick(ry);
    pad.connected = true;  // the keyboard is always a controller on port 1
}

}  // namespace kbm

#else  // the Switch and the Linux harness have no keyboard and mouse here
#include "common/kbm.h"
namespace kbm {
void Load(const void*) {}
void Save(void*) {}
void Window(void*) {}
bool Message(void*, uint32_t, uintptr_t, intptr_t, intptr_t*) { return false; }
void Pad(int, input::Pad&) {}
}  // namespace kbm
#endif
