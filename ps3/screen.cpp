// The TV picture: two RSX colour buffers at the console's video mode, flipped on vertical blank. Show() scales a
// frame (RGBA8, red in the low byte) into the back buffer, 4:3 in the middle, and flips to it.
#include "screen.h"

#include <algorithm>
#include <cstring>
#include <malloc.h>
#include <mutex>
#include <unistd.h>
#include <vector>

#include <rsx/rsx.h>
#include <sysutil/video.h>

#include "core/log.h"

namespace ps3 {

namespace {

constexpr uint32_t kCommandBuffer = 0x80000, kHostSize = 1u << 20;
gcmContextData* g_ctx = nullptr;
uint32_t g_w = 0, g_h = 0, g_pitch = 0, g_offset[2] = {}, g_cur = 0;
uint32_t* g_buffer[2] = {};
bool g_first = true;
std::mutex g_lock;

void WaitFlip() {
    while (gcmGetFlipStatus() != 0) usleep(200);
    gcmResetFlipStatus();
}

}  // namespace

bool InitScreen() {
    void* host = memalign(1u << 20, kHostSize);
    if (!host || rsxInit(&g_ctx, kCommandBuffer, kHostSize, host) || !g_ctx) {
        LOGE("ps3: the RSX did not start");
        return false;
    }
    videoState state;
    videoResolution res;
    if (videoGetState(0, 0, &state) || videoGetResolution(state.displayMode.resolution, &res)) {
        LOGE("ps3: no video mode");
        return false;
    }
    videoConfiguration vc;
    std::memset(&vc, 0, sizeof vc);
    vc.resolution = state.displayMode.resolution;
    vc.format = VIDEO_BUFFER_FORMAT_XRGB;
    vc.pitch = res.width * 4;
    videoConfigure(0, &vc, nullptr, 0);
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    g_w = res.width;
    g_h = res.height;
    g_pitch = g_w * 4;
    for (int i = 0; i < 2; i++) {
        g_buffer[i] = static_cast<uint32_t*>(rsxMemalign(64, g_h * g_pitch));
        if (!g_buffer[i]) {
            LOGE("ps3: no RSX memory for the screen");
            return false;
        }
        std::memset(g_buffer[i], 0, g_h * g_pitch);
        rsxAddressToOffset(g_buffer[i], &g_offset[i]);
        gcmSetDisplayBuffer(i, g_offset[i], g_pitch, g_w, g_h);
    }
    LOGI("ps3: screen %ux%u", g_w, g_h);
    return true;
}

void Show(const uint32_t* rgba, uint32_t w, uint32_t h) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (!g_ctx || !w || !h) return;
    uint32_t* dst = g_buffer[g_cur];
    // 4:3 at the screen's height, black bars at the sides
    uint32_t ow = std::min(g_w, g_h * 4 / 3), oh = g_h, ox = (g_w - ow) / 2;
    static std::vector<uint32_t> xs;
    xs.resize(ow);
    for (uint32_t x = 0; x < ow; x++) xs[x] = x * w / ow;
    static std::vector<uint32_t> row;
    row.assign(g_w, 0);
    for (uint32_t y = 0; y < oh; y++) {
        const uint32_t* src = rgba + size_t(y * h / oh) * w;
        for (uint32_t x = 0; x < ow; x++) {
            uint32_t c = src[xs[x]];  // R G B A from the low byte up
            row[ox + x] = (c & 0xFF) << 16 | (c & 0xFF00) | (c >> 16 & 0xFF);  // X8R8G8B8
        }
        std::memcpy(dst + size_t(y) * (g_pitch / 4), row.data(), g_pitch);
    }
    if (!g_first) WaitFlip();
    else gcmResetFlipStatus();
    gcmSetFlip(g_ctx, g_cur);
    rsxFlushBuffer(g_ctx);
    gcmSetWaitFlip(g_ctx);
    g_cur ^= 1;
    g_first = false;
}

}  // namespace ps3
