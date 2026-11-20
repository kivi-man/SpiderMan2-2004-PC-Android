// The PS3 controllers as Xbox pads: cross A, circle B, square X, triangle Y, L1 white, R1 black, L2 and R2 the
// triggers, select back, L3 and R3 the thumb buttons. The pad reports only when something changed, so the last
// report is kept per port.
// pad.txt in the port's folder: presses for port 0 from a script, lines of "seconds buttons [seconds held]" (held
// 0.3 s by default) counted from the start, # comments, buttons joined by +: UP DOWN LEFT RIGHT START BACK A B X Y
// BLACK WHITE LT RT LTHUMB RTHUMB, LS LSD LSL LSR (left stick up, down, left, right)
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <io/pad.h>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"
#include "ps3/screen.h"

namespace input {

namespace {
std::mutex g_lock;
Pad g_last[4];
bool g_ready = false;

struct Press {
    double at, len;
    std::string buttons;
};
std::vector<Press> g_script;
std::chrono::steady_clock::time_point g_start;

void Apply(const std::string& s, Pad& p) {
    size_t at = 0;
    while (at <= s.size()) {
        size_t e = s.find('+', at);
        if (e == std::string::npos) e = s.size();
        std::string b = s.substr(at, e - at);
        at = e + 1;
        static const char* names[] = {"UP", "DOWN", "LEFT", "RIGHT", "START", "BACK", "LTHUMB", "RTHUMB"};
        for (int i = 0; i < 8; i++)
            if (b == names[i]) p.buttons |= uint16_t(1 << i);
        static const char* analog[] = {"A", "B", "X", "Y", "BLACK", "WHITE", "LT", "RT"};
        for (int i = 0; i < 8; i++)
            if (b == analog[i]) p.analog[i] = 255;
        if (b == "LS") p.ly = 32767;
        if (b == "LSD") p.ly = -32767;
        if (b == "LSL") p.lx = -32767;
        if (b == "LSR") p.lx = 32767;
    }
}

int16_t Axis(unsigned v, bool flip) {
    int d = int(v) - 128;
    if (flip) d = -d;
    if (d > -8 && d < 8) d = 0;  // the sticks rest a little off centre
    return int16_t(d >= 127 ? 32767 : d <= -128 ? -32768 : d * 256);
}
}  // namespace

void Init() {
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_ready) g_ready = ioPadInit(7) == 0;
    LOGI("ps3: pads %s", g_ready ? "ready" : "did not start");
    g_start = std::chrono::steady_clock::now();
    if (FILE* f = std::fopen((ps3::g_dir / "pad.txt").string().c_str(), "r")) {
        char line[160], b[64];
        while (std::fgets(line, sizeof line, f)) {
            double t, len = 0.3;
            if (line[0] != '#' && std::sscanf(line, "%lf %63s %lf", &t, b, &len) >= 2) g_script.push_back({t, len, b});
        }
        std::fclose(f);
        LOGI("ps3: pad script, %u presses", unsigned(g_script.size()));
    }
}

Pad Poll(int port) {
    Pad p;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        if (g_ready && port >= 0 && port < 4) {
            padInfo info;
            if (ioPadGetInfo(&info) == 0 && info.status[port]) {
                padData d;
                std::memset(&d, 0, sizeof d);
                if (ioPadGetData(port, &d) == 0 && d.len > 0) {
                    Pad& n = g_last[port];
                    n = Pad{};
                    n.buttons = uint16_t((d.BTN_UP ? 1 : 0) | (d.BTN_DOWN ? 2 : 0) | (d.BTN_LEFT ? 4 : 0) | (d.BTN_RIGHT ? 8 : 0) |
                                         (d.BTN_START ? 16 : 0) | (d.BTN_SELECT ? 32 : 0) | (d.BTN_L3 ? 64 : 0) | (d.BTN_R3 ? 128 : 0));
                    n.analog[0] = d.BTN_CROSS ? 255 : 0;
                    n.analog[1] = d.BTN_CIRCLE ? 255 : 0;
                    n.analog[2] = d.BTN_SQUARE ? 255 : 0;
                    n.analog[3] = d.BTN_TRIANGLE ? 255 : 0;
                    n.analog[4] = d.BTN_R1 ? 255 : 0;  // black
                    n.analog[5] = d.BTN_L1 ? 255 : 0;  // white
                    n.analog[6] = d.BTN_L2 ? 255 : 0;
                    n.analog[7] = d.BTN_R2 ? 255 : 0;
                    n.lx = Axis(d.ANA_L_H, false);
                    n.ly = Axis(d.ANA_L_V, true);
                    n.rx = Axis(d.ANA_R_H, false);
                    n.ry = Axis(d.ANA_R_V, true);
                }
                p = g_last[port];
                p.connected = true;
            } else {
                g_last[port] = Pad{};
            }
        }
    }
    if (port == 0 && !g_script.empty()) {
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
        for (const Press& s : g_script)
            if (t >= s.at && t <= s.at + s.len) {
                Apply(s.buttons, p);
                p.connected = true;
            }
    }
    if (app::kGame.pad) app::kGame.pad(port, p);
    return p;
}

void Rumble(int port, uint16_t left, uint16_t right) {
    if (app::kGame.rumble) app::kGame.rumble(port, left, right);
    (void)port;
    (void)left;
    (void)right;  // the DualShock 3's motors: a later step
}

}  // namespace input
