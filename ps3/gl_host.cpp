// The GPU renderer on the PS3: gl/ (the Switch's OpenGL renderer) on RSXGL, with the CPU renderer (backend.cpp) when
// it does not start. RSXGL's context is made on the thread that makes the device, the one that then draws (no render
// thread on the PS3).
//
// The command buffer: libgcm hands RSXGL 32 KB windows of it and, at the end of each, waits for the GPU; RPCS3 takes
// ~300 ms there, every few hundred draws. The whole buffer is used as one ring instead: written to its end, then a
// jump back to its start, after the GPU has read up to the jump.
//
// capture.txt in the port's folder (tests): seconds from the start, one or more a line; the frame shown first at each
// is saved as cap/frame_<seconds>.tga (read back from the RSX: RPCS3 needs Write Color Buffers on for it).
#include <EGL/egl.h>
#include <GL3/rsxgl.h>
#include <rsx/gcm_sys.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/log.h"
#include "gl/gl_api.h"
#include "gl/gl_backend.h"
#include "gpu/backend.h"
#include "ps3/screen.h"

extern "C" gcmContextData* rsx_gcm_context;  // RSXGL's (egl.c)

namespace ps3 {

void* GlProc(const char* name);  // gl_procs.cpp

namespace {

// in main memory, shared with the RSX (RSXGL keeps buffers and textures in the RSX's own memory)
constexpr uint32_t kSharedSize = 8u << 20, kCommandSize = 6u << 20;

namespace ring {
uint32_t* g_start;
uint32_t* g_end;
uint32_t g_start_offset;
unsigned g_wraps;

struct Opd32 {  // libgcm calls the callback through a 32-bit function descriptor: entry, TOC
    uint32_t entry, toc;
} g_opd __attribute__((aligned(8)));

void WaitFor(uint32_t offset) {
    while (gcmGetControlRegister()->get != offset) usleep(20);
}

s32 Wrap(gcmContextData* ctx, u32) {
    *ctx->current = 0x20000000u | g_start_offset;  // jump to the start
    __asm__ __volatile__("sync" ::: "memory");
    gcmGetControlRegister()->put = g_start_offset;
    WaitFor(g_start_offset);  // everything up to the jump done: the ring is free
    ctx->begin = g_start;
    ctx->current = g_start;
    ctx->end = g_end;
    g_wraps++;
    return 0;
}

void Install(gcmContextData* ctx, uint32_t bytes) {
    uint32_t* io = reinterpret_cast<uint32_t*>(uintptr_t(ctx->begin) & ~uintptr_t(0xFFFFF));  // the 1 MB aligned io area
    g_start = io + 0x1000 / 4;                                                               // libgcm keeps 4 KB
    g_end = io + bytes / 4 - 16;                                                             // room for the jump
    gcmAddressToOffset(g_start, &g_start_offset);
    // what is queued drained, then the ring from its start
    uint32_t cur;
    gcmAddressToOffset(ctx->current, &cur);
    __asm__ __volatile__("sync" ::: "memory");
    gcmGetControlRegister()->put = cur;
    WaitFor(cur);
    *ctx->current = 0x20000000u | g_start_offset;
    __asm__ __volatile__("sync" ::: "memory");
    gcmGetControlRegister()->put = g_start_offset;
    WaitFor(g_start_offset);
    ctx->begin = g_start;
    ctx->current = g_start;
    ctx->end = g_end;
    const uint64_t* opd64 = reinterpret_cast<const uint64_t*>(reinterpret_cast<const void*>(&Wrap));
    g_opd.entry = uint32_t(opd64[0]);
    g_opd.toc = uint32_t(opd64[1]);
    ctx->callback = reinterpret_cast<gcmContextCallback>(reinterpret_cast<uintptr_t>(&g_opd));
    LOGI("ps3: command buffer ring of %u KB", unsigned((g_end - g_start) / 256));
}
}  // namespace ring

class RsxHost final : public glr::Host {
public:
    bool MakeCurrent() override {
        if (ready_) return true;
        if (tried_) return false;
        tried_ = true;
        rsxgl_init_parameters_t ip = {};
        ip.gcm_buffer_size = kSharedSize;
        ip.command_buffer_length = kCommandSize / 4;
        ip.max_swap_wait_iterations = 100000;
        ip.swap_wait_interval = 30;
        rsxglConfigure(&ip);
        dpy_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major = 0, minor = 0;
        if (dpy_ == EGL_NO_DISPLAY || !eglInitialize(dpy_, &major, &minor)) {
            LOGE("ps3: RSXGL did not start (eglInitialize)");
            return false;
        }
        const EGLint attribs[] = {EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE};
        EGLConfig config;
        EGLint n = 0;
        if (!eglChooseConfig(dpy_, attribs, &config, 1, &n) || n < 1) {
            LOGE("ps3: RSXGL has no 32-bit colour, 24-bit depth configuration");
            return false;
        }
        surface_ = eglCreateWindowSurface(dpy_, config, 0, nullptr);
        ctx_ = eglCreateContext(dpy_, config, EGL_NO_CONTEXT, nullptr);
        if (surface_ == EGL_NO_SURFACE || ctx_ == EGL_NO_CONTEXT || !eglMakeCurrent(dpy_, surface_, surface_, ctx_)) {
            LOGE("ps3: no RSXGL context");
            return false;
        }
        EGLint w = 0, h = 0;
        eglQuerySurface(dpy_, surface_, EGL_WIDTH, &w);
        eglQuerySurface(dpy_, surface_, EGL_HEIGHT, &h);
        w_ = uint32_t(w), h_ = uint32_t(h);
        ring::Install(rsx_gcm_context, kCommandSize);
        const char* missing = nullptr;
        if (!glapi::Load(&GlProc, &missing)) {
            LOGE("ps3: GL function %s missing", missing ? missing : "?");
            return false;
        }
        LOGI("ps3: RSXGL, EGL %d.%d, screen %ux%u", major, minor, w_, h_);
        ready_ = true;
        return true;
    }
    void Release() override {}
    void ScreenSize(uint32_t& w, uint32_t& h) override {
        w = w_;
        h = h_;
    }
    std::string CachePath() override { return (g_dir / "shaders.cache").string(); }
    glr::Picture GetPicture() override {
        glr::Picture p;
        p.sharp_movies = false;  // its filter reads textureSize in a function: not on RSXGL
        return p;
    }
    void Swap() override {
        Capture();
        eglSwapBuffers(dpy_, surface_);
    }

private:
    // capture.txt's next time due: the frame about to be shown, as a TGA
    void Capture() {
        using Clock = std::chrono::steady_clock;
        if (!capture_read_) {
            capture_read_ = true;
            start_ = Clock::now();
            if (FILE* f = std::fopen((g_dir / "capture.txt").string().c_str(), "r")) {
                double t;
                while (std::fscanf(f, "%lf", &t) == 1) captures_.push_back(t);
                std::fclose(f);
                LOGI("ps3: %u frames to capture", unsigned(captures_.size()));
            }
        }
        if (captures_.empty()) return;
        double now = std::chrono::duration<double>(Clock::now() - start_).count();
        size_t due = captures_.size();
        for (size_t i = 0; i < captures_.size(); i++)
            if (captures_[i] <= now) due = i;
        if (due == captures_.size()) return;
        double at = captures_[due];
        captures_.erase(captures_.begin() + long(due));
        std::vector<uint8_t> px(size_t(w_) * h_ * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadPixels(0, 0, GLsizei(w_), GLsizei(h_), GL_BGRA, GL_UNSIGNED_BYTE, px.data());
        std::error_code ec;
        std::filesystem::create_directories(g_dir / "cap", ec);
        char name[64];
        std::snprintf(name, sizeof name, "frame_%04d.tga", int(at));
        if (FILE* o = std::fopen((g_dir / "cap" / name).string().c_str(), "wb")) {
            uint8_t hdr[18] = {0, 0, 2};  // uncompressed true colour, rows from the bottom (GL's order)
            hdr[12] = uint8_t(w_), hdr[13] = uint8_t(w_ >> 8), hdr[14] = uint8_t(h_), hdr[15] = uint8_t(h_ >> 8), hdr[16] = 32, hdr[17] = 8;
            std::fwrite(hdr, 1, sizeof hdr, o);
            std::fwrite(px.data(), 1, px.size(), o);
            std::fclose(o);
        }
        LOGI("ps3: captured %s", name);
    }

    bool capture_read_ = false;
    std::vector<double> captures_;
    std::chrono::steady_clock::time_point start_;
    bool tried_ = false, ready_ = false;
    EGLDisplay dpy_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext ctx_ = EGL_NO_CONTEXT;
    uint32_t w_ = 1280, h_ = 720;
};

}  // namespace

}  // namespace ps3

namespace gpu {

std::unique_ptr<Backend> CreateCpuBackend();  // backend.cpp

namespace {

// the GPU renderer, or the CPU one when it does not start (XBE_GL=0: the CPU one from the start)
class FirstGl final : public Backend {
public:
    bool Init(uint32_t w, uint32_t h) override {
        static ps3::RsxHost host;
        const char* want = std::getenv("XBE_GL");
        if (!want || std::string(want) != "0") {
            inner_ = glr::CreateBackend(&host);
            if (inner_ && inner_->Init(w, h)) return true;
            LOGE("renderer: the GPU renderer did not start, the CPU one draws");
        }
        inner_ = CreateCpuBackend();
        return inner_->Init(w, h);
    }
    void Clear(const State& s, uint32_t n, const uint8_t* rects, uint32_t flags, uint32_t color, float z, uint32_t stencil) override {
        inner_->Clear(s, n, rects, flags, color, z, stencil);
    }
    void Draw(const State& s, uint32_t prim, uint32_t start, uint32_t count, const uint8_t* indices) override {
        inner_->Draw(s, prim, start, count, indices);
    }
    void Copy(const Surface& src, const Surface& dst, uint32_t n, uint32_t rects, uint32_t points) override {
        inner_->Copy(src, dst, n, rects, points);
    }
    void Present(const Surface& front) override { inner_->Present(front); }
    void Sync(uint32_t data, uint32_t size) override { inner_->Sync(data, size); }
    void SyncLate(uint32_t data) override { inner_->SyncLate(data); }
    void Flush() override { inner_->Flush(); }
    void SetGamma(const uint8_t* ramp) override { inner_->SetGamma(ramp); }
    void SetSwapInterval(uint32_t blanks) override { inner_->SetSwapInterval(blanks); }
    void ClearReport() override { inner_->ClearReport(); }
    void GetReport(uint32_t r) override { inner_->GetReport(r); }

private:
    std::unique_ptr<Backend> inner_;
};

}  // namespace

std::unique_ptr<Backend> CreateBackend() { return std::make_unique<FirstGl>(); }

}  // namespace gpu
