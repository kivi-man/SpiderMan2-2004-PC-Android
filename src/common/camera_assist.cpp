// The game turns the camera from the right stick (XBE 0x16AE0D): yaw = clamp(1 + ramp, 1, 2)^2 * speed * dt * 0.65 *
// stick, pitch the same with its own fields. Below a threshold (0.4) an axis counts as idle; above it the ramp grows
// with time, up to 4x the speed after a second. The controller-camera object is found through a static pointer and
// checked by its vtable before anything is written.
#include "common/camera_assist.h"

#include <cstdint>
#include <cstring>
#include <mutex>

#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace camera_assist {

namespace {

constexpr uint32_t kCameraPtr = 0x004F3AF0, kCameraVtable = 0x004629F8;
constexpr uint32_t kThresholdX = 0x954, kThresholdY = 0x984, kRampX = 0x964, kRampY = 0x994, kYawSpeed = 0xA7C,
                   kPitchSpeed = 0xA80;

struct Camera {
    uint32_t obj = 0;
    float yaw = 2, pitch = 1.5f, threshold_x = 0.4f, threshold_y = 0.4f;
    bool boosted = false;
};
Camera g_cam;
std::mutex g_lock;

// through the kernel's guest pointer: flat on the PC and Android, segmented on the Switch
bool Readable(uint32_t va) { return mem::Base() && mem::IsCommitted(va) && mem::IsCommitted(va + 3); }
uint32_t Get32(uint32_t va) { return kernel::R32(va); }
float GetF(uint32_t va) { return kernel::RF(va); }
void SetF(uint32_t va, float v) { kernel::WF(va, v); }

uint32_t CameraObject() {
    if (!Readable(kCameraPtr)) return 0;
    uint32_t p = Get32(kCameraPtr);
    if (p < 0x00540000 || p >= 0x14000000 || !Readable(p) || !Readable(p + kPitchSpeed)) return 0;
    return Get32(p) == kCameraVtable ? p : 0;
}

}  // namespace

void Update(bool active, float speed) {
    std::lock_guard lock(g_lock);
    uint32_t obj = CameraObject();
    if (obj != g_cam.obj) {
        g_cam = {};
        g_cam.obj = obj;
        if (obj) {
            g_cam.yaw = GetF(obj + kYawSpeed);
            g_cam.pitch = GetF(obj + kPitchSpeed);
            g_cam.threshold_x = GetF(obj + kThresholdX);
            g_cam.threshold_y = GetF(obj + kThresholdY);
            LOGI("camera: object %08X, turn speeds %.2f %.2f", obj, g_cam.yaw, g_cam.pitch);
        }
    }
    if (!obj) return;
    if (active) {
        float k = speed * 4.0f;  // a fully ramped stick's speed, times speed
        SetF(obj + kThresholdX, 0.001f);
        SetF(obj + kThresholdY, 0.001f);
        SetF(obj + kRampX, 0);
        SetF(obj + kRampY, 0);
        SetF(obj + kYawSpeed, g_cam.yaw * k);
        SetF(obj + kPitchSpeed, g_cam.pitch * k);
        g_cam.boosted = true;
    } else if (g_cam.boosted) {
        SetF(obj + kThresholdX, g_cam.threshold_x);
        SetF(obj + kThresholdY, g_cam.threshold_y);
        SetF(obj + kYawSpeed, g_cam.yaw);
        SetF(obj + kPitchSpeed, g_cam.pitch);
        g_cam.boosted = false;
    }
}

}  // namespace camera_assist
