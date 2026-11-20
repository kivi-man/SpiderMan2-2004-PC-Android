// The PS3 CPU renderer, the GPU one's fallback (gl_host.cpp): the harness's CPU renderer, as the Switch's fallback uses it.
// A frame is drawn into guest memory now and then and that frame goes to the TV; in between frames are counted and
// paced at the game's rate without being drawn, so the game runs at its speed while the picture updates.
#include <algorithm>
#include <chrono>
#include <map>
#include <thread>
#include <vector>

#include "core/log.h"
#include "gpu/backend.h"
#include "gpu/texture.h"
#include "kernel/kernel.h"
#include "ps3/screen.h"
#include "reference_renderer.h"

namespace gpu {

namespace {

using Clock = std::chrono::steady_clock;

class Ps3Backend final : public Backend {
public:
    bool Init(uint32_t w, uint32_t h) override {
        LOGI("renderer: PS3 CPU renderer, %ux%u", w, h);
        if (!ps3::InitScreen()) LOGE("ps3: no picture");  // the GPU renderer, when it runs, has the RSX instead
        next_draw_ = last_present_ = Clock::now();
        return true;
    }
    void Clear(const State& s, uint32_t n, const uint8_t* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil) override {
        if (drawing_) reference::Clear(s, n, rects, flags, color, z, stencil);
    }
    void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override {
        draws_++;
        if (drawing_) reference::Draw(s, prim, start, count, indices);
    }
    void Copy(const Surface& src, const Surface& dst, uint32_t n, uint32_t rects, uint32_t points) override {
        if (drawing_) reference::Copy(src, dst, n, rects, points);
        else CopySurfaceMemory(src, dst, n, rects, points);
    }
    void Present(const Surface& front) override {
        auto now = Clock::now();
        if (drawing_) {
            uint32_t w = 0, h = 0;
            if ((shown_ || draws_ > draws_at_draw_) && reference::ReadRgba(front, pixels_, w, h)) {
                if (!shown_) LOGI("renderer: first picture, %llu draws in it", draws_ - draws_at_draw_);
                ps3::Show(pixels_.data(), w, h);
                shown_++;
            }
            auto took = std::chrono::duration_cast<std::chrono::milliseconds>(now - draw_start_).count();
            // as much time running undrawn as the last drawing took, at least half a second
            next_draw_ = now + std::chrono::milliseconds(std::max<long long>(500, took));
            if (drawn_++ < 20 || drawn_ % 50 == 0)
                LOGI("renderer: frame %llu drawn in %lld ms, %llu draws so far", frames_, (long long)took, draws_);
        } else {
            auto due = last_present_ + std::chrono::microseconds(16667 * std::max<uint32_t>(blanks_, 1));
            if (now < due) std::this_thread::sleep_until(due);
        }
        last_present_ = Clock::now();
        frames_++;
        if (frames_ % 600 == 0) LOGI("renderer: frame %llu, %llu draws", frames_, draws_);
        drawing_ = last_present_ >= next_draw_;
        if (drawing_) {
            draw_start_ = last_present_;
            draws_at_draw_ = draws_;
            reference::BeginFrame();
        }
    }
    void Sync(uint32_t, uint32_t) override {}
    void SyncLate(uint32_t) override {}
    void Flush() override {}
    void SetGamma(const uint8_t*) override {}
    void SetSwapInterval(uint32_t blanks) override { blanks_ = blanks; }
    // visibility tests, as the harness answers them: the count of the last drawn frame, visible when never drawn
    void ClearReport() override { reference::ResetCount(); }
    void GetReport(uint32_t va) override {
        uint32_t count;
        if (drawing_) {
            count = uint32_t(std::min<uint64_t>(reference::CountedPixels(), 0xFFFFFFFF));
            counts_[va] = count;
        } else {
            auto it = counts_.find(va);
            count = it != counts_.end() ? it->second : 1000;
        }
        kernel::W32(va, uint32_t(frames_));
        kernel::W32(va + 4, 0);
        kernel::W32(va + 8, count);
        kernel::W32(va + 12, 0);
    }

private:
    uint32_t blanks_ = 1;
    unsigned long long frames_ = 0, draws_ = 0, shown_ = 0, drawn_ = 0, draws_at_draw_ = 0;
    bool drawing_ = false;
    Clock::time_point next_draw_, draw_start_, last_present_;
    std::vector<uint32_t> pixels_;
    std::map<uint32_t, uint32_t> counts_;
};

}  // namespace

// gl_host.cpp picks this one when the GPU renderer does not start
std::unique_ptr<Backend> CreateCpuBackend() { return std::make_unique<Ps3Backend>(); }

}  // namespace gpu
