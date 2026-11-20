// The renderer on Android: the GL one (gl/) on OpenGL ES 3.2, through the window's context, with the touch controls
// drawn over every frame.
#include <memory>

#include "android/src/droid.h"
#include "core/log.h"
#include "gl/gl_backend.h"
#include "gpu/backend.h"

namespace gpu {

namespace {

class AndroidHost final : public glr::Host {
public:
    bool MakeCurrent() override { return droid::GlTake(); }
    void Release() override { droid::GlRelease(); }
    void ScreenSize(uint32_t& w, uint32_t& h) override { droid::ScreenSize(w, h); }
    void Presented(uint32_t) override {
        uint32_t w, h;
        droid::ScreenSize(w, h);
        droid::DrawTouch(w, h);
    }
    void SetSwapInterval(uint32_t blanks) override { droid::GlSwapInterval(blanks); }
    std::string CachePath() override { return droid::Folder() + "/cache/shaders.cache"; }
    glr::Picture GetPicture() override {
        glr::Picture p;
        p.height = 720;
        p.wide = true;
        p.anisotropy = 4;
        return p;
    }
    void Swap() override { droid::GlSwap(); }
};

}  // namespace

std::unique_ptr<Backend> CreateBackend() {
    static AndroidHost host;
    return glr::CreateBackend(&host);
}

}  // namespace gpu
