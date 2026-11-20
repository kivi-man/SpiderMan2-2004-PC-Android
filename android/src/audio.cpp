// Sound on Android through SDL3's audio stream: 48 kHz stereo float, the SDK's DirectSound mixer pulled as the device
// asks. Without a device the mixer is still pulled every 20 ms, so the game's streams keep moving.
#include <SDL3/SDL.h>

#include <chrono>
#include <thread>
#include <vector>

#include "audio/audio.h"
#include "core/log.h"

namespace audio {

namespace {
std::function<void(float*, uint32_t)> g_render;

void SDLCALL Pull(void*, SDL_AudioStream* stream, int additional, int) {
    static std::vector<float> mix;
    uint32_t frames = uint32_t(additional) / (2 * sizeof(float));
    if (!frames) return;
    mix.resize(size_t(frames) * 2);
    g_render(mix.data(), frames);
    SDL_PutAudioStreamData(stream, mix.data(), int(mix.size() * sizeof(float)));
}
}  // namespace

void StartOutput(std::function<void(float*, uint32_t)> render) {
    g_render = std::move(render);
    SDL_AudioSpec spec{SDL_AUDIO_F32, 2, int(kRate)};
    SDL_AudioStream* s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Pull, nullptr);
    if (!s) {
        LOGE("audio: no sound device (%s), the mixer runs without output", SDL_GetError());
        std::thread([] {
            std::vector<float> mix(kRate / 50 * 2);
            for (;;) {
                g_render(mix.data(), kRate / 50);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }).detach();
        return;
    }
    SDL_ResumeAudioStreamDevice(s);
    LOGI("audio: SDL audio at %u Hz", kRate);
}

}  // namespace audio
