// Controllers and the touch controls on Android, read into the console controller's report.
// Controllers (Bluetooth or USB: Xbox, PlayStation, Switch pads) through SDL3 take the ports in the order they come.
// The touch controls play as controller 1 beside a controller there: a stick that appears where the left thumb lands
// and follows it, the face buttons, the triggers and Start/Back, and a drag anywhere else on the right half turns the
// camera 1:1 (src/common/camera_assist). controls.ini in the app's folder sets the camera's sensitivity, the controls'
// size and opacity.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "android/src/droid.h"
#include "app/app.h"
#include "common/camera_assist.h"
#include "core/input.h"
#include "core/log.h"
#include "gl/gl_backend.h"

namespace droid {

namespace {

// ------------------------------------------------------------------------------------------------ controllers
std::mutex g_pad_lock;
SDL_Gamepad* g_pads[4] = {};

// ------------------------------------------------------------------------------------------------ settings
struct Settings {
    float camera_sensitivity = 1.0f;  // 1: a drag across the screen's width turns the camera about half around
    bool invert_y = false;
    float size = 1.0f;     // the controls' size
    float opacity = 1.0f;  // how visible they are
    float stick_deadzone = 0.24f;  // the game's own, skipped: the smallest push moves Spider-Man
};
Settings g_set;

void LoadSettings() {
    const std::string path = Folder() + "/controls.ini";
    std::ifstream f(path);
    if (!f) {
        if (FILE* o = std::fopen(path.c_str(), "w")) {
            std::fputs("; Spider-Man 2 touch controls. Changes apply when the game starts again.\n"
                       "; camera speed for a drag: 1 = across the screen's width turns about half around; 2 twice that\n"
                       "camera_sensitivity = 1.0\n"
                       "; 1: drag up to look down\n"
                       "invert_y = 0\n"
                       "; size of the buttons and the stick, 1 = normal (0.7 to 1.5)\n"
                       "size = 1.0\n"
                       "; how visible the controls are, 0.3 to 1.5\n"
                       "opacity = 1.0\n",
                       o);
            std::fclose(o);
        }
        return;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        key.erase(key.find_last_not_of(" \t") + 1);
        float v = std::strtof(line.c_str() + eq + 1, nullptr);
        if (key == "camera_sensitivity") g_set.camera_sensitivity = std::clamp(v, 0.1f, 10.0f);
        else if (key == "invert_y") g_set.invert_y = v != 0;
        else if (key == "size") g_set.size = std::clamp(v, 0.5f, 2.0f);
        else if (key == "opacity") g_set.opacity = std::clamp(v, 0.1f, 2.0f);
    }
    LOGI("input: touch camera %.2f, invert %d, size %.2f, opacity %.2f", g_set.camera_sensitivity, int(g_set.invert_y),
         g_set.size, g_set.opacity);
}

// ------------------------------------------------------------------------------------------------ touch
// what a touch button presses: a bit of Pad::buttons (dpad up/down/left/right, start, back, thumbs) or an analog
enum class Kind : uint8_t { Button, Analog };
struct Control {
    const char* label;
    Kind kind;
    int index;      // the bit or the analog slot (A B X Y black white LT RT)
    float x, y, r;  // centre and radius, in screen pixels (from the layout)
};
std::vector<Control> g_controls;
float g_home_x = 0, g_home_y = 0, g_stick_r = 0;  // the stick's resting place and reach
uint32_t g_layout_w = 0, g_layout_h = 0;

struct Finger {
    enum class Role : uint8_t { None, Stick, Camera, Control } role = Role::None;
    int control = -1;
    float x0 = 0, y0 = 0;  // where the stick's centre is
    float x = 0, y = 0;
};
std::mutex g_touch_lock;
std::map<uint64_t, Finger> g_fingers;
float g_cam_dx = 0, g_cam_dy = 0;  // camera drag since the last poll, pixels
float g_look_x = 0, g_look_y = 0;
uint64_t g_last_poll = 0, g_last_drag = 0;
uint64_t g_back_until = 0;  // the back button holds Start until then (ms)

void Layout(uint32_t w, uint32_t h) {
    if (w == g_layout_w && h == g_layout_h) return;
    g_layout_w = w, g_layout_h = h;
    const float H = float(h), W = float(w), k = g_set.size;
    const float r = 0.085f * H * k, s = 0.055f * H * k;
    const float cx = W - 0.27f * H * k, cy = H - 0.30f * H * k, d = 0.16f * H * k;
    g_controls = {
        {"A", Kind::Analog, 0, cx, cy + d, r},
        {"B", Kind::Analog, 1, cx + d, cy, r},
        {"X", Kind::Analog, 2, cx - d, cy, r},
        {"Y", Kind::Analog, 3, cx, cy - d, r},
        {"RT", Kind::Analog, 7, W - 0.13f * H * k, H - 0.68f * H * k, r * 1.1f},
        {"LT", Kind::Analog, 6, 0.13f * H * k, 0.30f * H * k, r},
        {"BLK", Kind::Analog, 4, W - 0.36f * H * k, H - 0.70f * H * k, s},
        {"WHT", Kind::Analog, 5, W - 0.56f * H * k, H - 0.58f * H * k, s},
        {"START", Kind::Button, 4, W * 0.5f + 0.13f * H, 0.08f * H, s},
        {"BACK", Kind::Button, 5, W * 0.5f - 0.13f * H, 0.08f * H, s},
    };
    g_stick_r = 0.13f * H * k;
    g_home_x = 0.26f * H * k, g_home_y = H - 0.27f * H * k;
}

int Hit(float x, float y) {
    for (size_t i = 0; i < g_controls.size(); i++) {
        const Control& c = g_controls[i];
        float dx = x - c.x, dy = y - c.y, rr = c.r * 1.3f;
        if (dx * dx + dy * dy <= rr * rr) return int(i);
    }
    return -1;
}

// a stick's offset, its length past the game's dead zone so the smallest push counts
void StickOut(float dx, float dy, float reach, float dead, float& ox, float& oy) {
    float len = std::sqrt(dx * dx + dy * dy) / reach;
    ox = oy = 0;
    if (len < 0.06f) return;
    float out = dead + (1.0f - dead) * std::min(len, 1.0f);
    ox = dx / (len * reach) * out, oy = dy / (len * reach) * out;
}

// ------------------------------------------------------------------------------------------------ drawing
// a 5x7 font for the labels
const std::map<char, const char*> kGlyphs = {
    {'A', "01110100011000111111100011000110001"}, {'B', "11110100011000111110100011000111110"},
    {'C', "01110100011000010000100001000101110"}, {'H', "10001100011000111111100011000110001"},
    {'K', "10001100101010011000101001001010001"}, {'L', "10000100001000010000100001000011111"},
    {'R', "11110100011000111110101001001010001"}, {'S', "01111100001000001110000010000111110"},
    {'T', "11111001000010000100001000010000100"}, {'W', "10001100011000110101101011101110001"},
    {'X', "10001100010101000100010101000110001"}, {'Y', "10001100010101000100001000010000100"},
};

// RGBA8, red in the low byte; alpha scaled by the opacity setting
uint32_t Rgba(uint8_t r, uint8_t g, uint8_t b, int a) {
    a = std::clamp(int(float(a) * g_set.opacity), 0, 255);
    return r | g << 8 | b << 16 | uint32_t(a) << 24;
}

void Disc(std::vector<uint32_t>& px, uint32_t w, uint32_t h, float cx, float cy, float r, uint32_t fill, uint32_t ring,
          float ring_w = 3.0f) {
    int x0 = std::max(0, int(cx - r - 2)), x1 = std::min(int(w) - 1, int(cx + r + 2));
    int y0 = std::max(0, int(cy - r - 2)), y1 = std::min(int(h) - 1, int(cy + r + 2));
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            if (d <= r - ring_w) px[size_t(y) * w + size_t(x)] = fill;
            else if (d <= r) px[size_t(y) * w + size_t(x)] = ring;
        }
}

void Label(std::vector<uint32_t>& px, uint32_t w, uint32_t h, float cx, float cy, const char* s, int scale, uint32_t color) {
    int n = int(std::strlen(s));
    int tw = n * 6 * scale - scale, x0 = int(cx) - tw / 2, y0 = int(cy) - 7 * scale / 2;
    for (int i = 0; i < n; i++) {
        auto g = kGlyphs.find(s[i]);
        if (g == kGlyphs.end()) continue;
        for (int gy = 0; gy < 7; gy++)
            for (int gx = 0; gx < 5; gx++) {
                if (g->second[gy * 5 + gx] != '1') continue;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) {
                        int x = x0 + i * 6 * scale + gx * scale + sx, y = y0 + gy * scale + sy;
                        if (x >= 0 && y >= 0 && x < int(w) && y < int(h)) px[size_t(y) * w + size_t(x)] = color;
                    }
            }
    }
}

}  // namespace

void GamepadAdded(uint32_t id) {
    std::lock_guard lock(g_pad_lock);
    for (SDL_Gamepad* p : g_pads)
        if (p && SDL_GetGamepadID(p) == id) return;
    for (int i = 0; i < 4; i++)
        if (!g_pads[i]) {
            g_pads[i] = SDL_OpenGamepad(id);
            const char* name = g_pads[i] ? SDL_GetGamepadName(g_pads[i]) : SDL_GetError();
            LOGI("input: controller %d %s: %s", i + 1, g_pads[i] ? "plugged in" : "failed to open", name ? name : "");
            return;
        }
}

void GamepadRemoved(uint32_t id) {
    std::lock_guard lock(g_pad_lock);
    for (int i = 0; i < 4; i++)
        if (g_pads[i] && SDL_GetGamepadID(g_pads[i]) == id) {
            SDL_CloseGamepad(g_pads[i]);
            g_pads[i] = nullptr;
            LOGI("input: controller %d unplugged", i + 1);
        }
}

void BackPressed() {
    std::lock_guard lock(g_touch_lock);
    g_back_until = SDL_GetTicks() + 120;
}

void TouchLayout(uint32_t w, uint32_t h) {
    std::lock_guard lock(g_touch_lock);
    Layout(w, h);
}

void TouchEvent(int type, uint64_t id, float nx, float ny) {
    g_touch_visible = true;
    uint32_t w = 0, h = 0;
    ScreenSize(w, h);
    std::lock_guard lock(g_touch_lock);
    Layout(w, h);
    float x = nx * float(w), y = ny * float(h);
    if (type == SDL_EVENT_FINGER_UP || type == SDL_EVENT_FINGER_CANCELED) {
        g_fingers.erase(id);
        return;
    }
    if (type == SDL_EVENT_FINGER_DOWN) {
        LOG_FIRST(40, "input: touch at %.3f %.3f = %.0f %.0f of %ux%u", nx, ny, x, y, w, h);
        Finger f;
        f.x = x, f.y = y;
        f.control = Hit(x, y);
        if (f.control >= 0) {
            f.role = Finger::Role::Control;
        } else if (x < float(w) * 0.45f) {
            // the stick comes to the thumb, kept whole on the screen
            f.role = Finger::Role::Stick;
            f.x0 = std::clamp(x, g_stick_r, float(w) * 0.45f);
            f.y0 = std::clamp(y, g_stick_r + 0.05f * float(h), float(h) - g_stick_r);
        } else {
            f.role = Finger::Role::Camera;
        }
        g_fingers[id] = f;
        return;
    }
    auto it = g_fingers.find(id);
    if (it == g_fingers.end()) return;
    Finger& f = it->second;
    if (f.role == Finger::Role::Camera) {
        g_cam_dx += x - f.x;
        g_cam_dy += y - f.y;
        g_last_drag = SDL_GetTicks();
    } else if (f.role == Finger::Role::Control) {
        // sliding from one button onto another presses that one (A to X and so on)
        int now = Hit(x, y);
        if (now >= 0) f.control = now;
    }
    f.x = x, f.y = y;
}

TouchPad TouchState() {
    TouchPad t;
    std::lock_guard lock(g_touch_lock);
    uint64_t now = SDL_GetTicks();
    float dt = g_last_poll ? std::clamp(float(now - g_last_poll) / 1000.0f, 0.001f, 0.1f) : 0.016f;
    g_last_poll = now;
    if (now < g_back_until) t.buttons |= 1 << 4;
    for (auto& [id, f] : g_fingers) {
        if (f.role == Finger::Role::Control && f.control >= 0 && size_t(f.control) < g_controls.size()) {
            const Control& c = g_controls[size_t(f.control)];
            if (c.kind == Kind::Button) t.buttons |= uint16_t(1 << c.index);
            else t.analog[c.index] = 0xFF;
        } else if (f.role == Finger::Role::Stick && g_stick_r > 0) {
            StickOut(f.x - f.x0, f.y0 - f.y, g_stick_r, g_set.stick_deadzone, t.lx, t.ly);
        }
    }
    // the camera: the drag's speed into a stick that the game then follows 1:1 (camera_assist). The game turns about
    // 5.2 radians a second at a full stick with no ramp, so a stick of v / (3.6 h) turns half around for a drag across
    // the width. The game turns against the stick, so a drag to the right is the stick to the left.
    const float h = g_layout_h ? float(g_layout_h) : 1080.0f;
    const float scale = g_set.camera_sensitivity / (3.6f * h);
    float vx = -g_cam_dx / dt * scale, vy = (g_set.invert_y ? -1.0f : 1.0f) * g_cam_dy / dt * scale;
    g_cam_dx = g_cam_dy = 0;
    float alpha = 1.0f - std::exp(-dt * 1000.0f / 20.0f);
    g_look_x += (vx - g_look_x) * alpha;
    g_look_y += (vy - g_look_y) * alpha;
    float mag = std::sqrt(g_look_x * g_look_x + g_look_y * g_look_y);
    if (mag > 0.002f) {
        float out = 0.214f + 0.786f * std::min(mag, 1.0f);  // past the game's dead zone, measured on the PC
        t.rx = g_look_x / mag * out, t.ry = g_look_y / mag * out;
    }
    t.camera = now - g_last_drag < 150;
    return t;
}

void DrawTouch(uint32_t w, uint32_t h) {
    if (!g_touch_visible.load()) return;
    // the buttons, made once: slot 3
    static std::vector<uint32_t> px;
    static uint32_t made_w = 0, made_h = 0;
    // a pressed button's glow (slot 0), the stick's ring (slot 1) and knob (slot 2), each made once and moved
    static std::vector<uint32_t> glow, ring, knob;
    static uint32_t glow_d = 0, ring_d = 0, knob_d = 0;
    bool upload = false;
    std::vector<std::pair<float, float>> pressed;
    float sx, sy, kx, ky, big_r = 0;
    {
        std::lock_guard lock(g_touch_lock);
        if (w != made_w || h != made_h) {
            Layout(w, h);
            px.assign(size_t(w) * h, 0);
            const uint32_t fill = Rgba(255, 255, 255, 40), edge = Rgba(255, 255, 255, 120), text = Rgba(255, 255, 255, 180);
            for (const Control& c : g_controls) {
                Disc(px, w, h, c.x, c.y, c.r, fill, edge);
                int scale = std::max(1, int(c.r / (std::strlen(c.label) > 2 ? 12.0f : 7.0f)));
                Label(px, w, h, c.x, c.y, c.label, scale, text);
            }
            made_w = w, made_h = h;
            upload = true;
            big_r = g_controls.empty() ? 40.0f : g_controls[0].r;
            glow_d = uint32_t(big_r * 2 + 4);
            glow.assign(size_t(glow_d) * glow_d, 0);
            Disc(glow, glow_d, glow_d, glow_d / 2.0f, glow_d / 2.0f, big_r, Rgba(255, 255, 255, 90), Rgba(255, 255, 255, 200), 5.0f);
            ring_d = uint32_t(g_stick_r * 2 + 4);
            ring.assign(size_t(ring_d) * ring_d, 0);
            Disc(ring, ring_d, ring_d, ring_d / 2.0f, ring_d / 2.0f, g_stick_r, Rgba(255, 255, 255, 25), Rgba(255, 255, 255, 120));
            knob_d = uint32_t(g_stick_r * 0.9f + 4);
            knob.assign(size_t(knob_d) * knob_d, 0);
            Disc(knob, knob_d, knob_d, knob_d / 2.0f, knob_d / 2.0f, g_stick_r * 0.45f, Rgba(255, 255, 255, 110), Rgba(255, 255, 255, 200));
        }
        // the stick at rest, or where the thumb holds it
        sx = g_home_x, sy = g_home_y, kx = sx, ky = sy;
        for (auto& [id, f] : g_fingers) {
            if (f.role == Finger::Role::Stick) {
                sx = f.x0, sy = f.y0;
                float dx = f.x - f.x0, dy = f.y - f.y0, len = std::sqrt(dx * dx + dy * dy);
                if (len > g_stick_r) dx *= g_stick_r / len, dy *= g_stick_r / len;
                kx = sx + dx, ky = sy + dy;
            } else if (f.role == Finger::Role::Control && f.control >= 0 && size_t(f.control) < g_controls.size()) {
                pressed.push_back({g_controls[size_t(f.control)].x, g_controls[size_t(f.control)].y});
            }
        }
    }
    glr::DrawOverlay(3, px.data(), w, h, 0, 0, w, h, upload);
    glr::DrawOverlay(1, ring.data(), ring_d, ring_d, int(sx) - int(ring_d / 2), int(sy) - int(ring_d / 2), w, h, upload);
    glr::DrawOverlay(2, knob.data(), knob_d, knob_d, int(kx) - int(knob_d / 2), int(ky) - int(knob_d / 2), w, h, upload);
    for (auto [x, y] : pressed)
        glr::DrawOverlay(0, glow.data(), glow_d, glow_d, int(x) - int(glow_d / 2), int(y) - int(glow_d / 2), w, h, upload);
}

}  // namespace droid

namespace input {

void Init() {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    droid::LoadSettings();
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; i++) droid::GamepadAdded(ids[i]);
        SDL_free(ids);
    }
}

Pad Poll(int port) {
    Pad p;
    {
        std::lock_guard lock(droid::g_pad_lock);
        if (SDL_Gamepad* pad = port >= 0 && port < 4 ? droid::g_pads[port] : nullptr) {
            p.connected = true;
            constexpr SDL_GamepadButton kButtons[] = {
                SDL_GAMEPAD_BUTTON_DPAD_UP,    SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_START,     SDL_GAMEPAD_BUTTON_BACK,
                SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK};
            for (int i = 0; i < 8; i++) p.buttons |= uint16_t(SDL_GetGamepadButton(pad, kButtons[i]) << i);
            constexpr SDL_GamepadButton kAnalog[] = {SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
                                                     SDL_GAMEPAD_BUTTON_WEST,  SDL_GAMEPAD_BUTTON_NORTH,
                                                     SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER};
            for (int i = 0; i < 6; i++) p.analog[i] = SDL_GetGamepadButton(pad, kAnalog[i]) ? 0xFF : 0;
            auto axis = [&](SDL_GamepadAxis a) { return int(SDL_GetGamepadAxis(pad, a)); };
            p.analog[6] = uint8_t(std::max(axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER), 0) >> 7);
            p.analog[7] = uint8_t(std::max(axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), 0) >> 7);
            p.lx = int16_t(axis(SDL_GAMEPAD_AXIS_LEFTX));
            p.ly = int16_t(-1 - axis(SDL_GAMEPAD_AXIS_LEFTY));
            p.rx = int16_t(axis(SDL_GAMEPAD_AXIS_RIGHTX));
            p.ry = int16_t(-1 - axis(SDL_GAMEPAD_AXIS_RIGHTY));
        }
    }
    if (port == 0) {
        // the touch controls on controller 1, over whatever a controller there does
        droid::TouchPad t = droid::TouchState();
        p.connected = true;
        p.buttons |= t.buttons;
        for (int i = 0; i < 8; i++) p.analog[i] = std::max(p.analog[i], t.analog[i]);
        auto stick = [](float v) { return int16_t(std::clamp(v, -1.0f, 1.0f) * 32767.0f); };
        if (t.lx || t.ly) p.lx = stick(t.lx), p.ly = stick(t.ly);
        if (t.rx || t.ry) p.rx = stick(t.rx), p.ry = stick(t.ry);
        camera_assist::Update(t.camera, 1.0f);
    }
    if (app::kGame.pad) app::kGame.pad(port, p);
    return p;
}

void Rumble(int port, uint16_t left, uint16_t right) {
    if (app::kGame.rumble) app::kGame.rumble(port, left, right);
    std::lock_guard lock(droid::g_pad_lock);
    if (port >= 0 && port < 4 && droid::g_pads[port]) SDL_RumbleGamepad(droid::g_pads[port], left, right, 0);
}

}  // namespace input
