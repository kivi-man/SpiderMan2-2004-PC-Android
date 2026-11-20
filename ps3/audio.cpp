// The mixer's output (48 kHz, stereo floats) into a 2 channel lv2 audio port: a thread keeps the blocks a few
// ahead of the one the system is reading.
#include <cstdint>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

// PSL1GHT's audio library by its longer path: the runtime's audio/audio.h comes first on the include path
#include <ppu/include/audio/audio.h>
#include <unistd.h>

#include "audio/audio.h"
#include "core/log.h"

namespace audio {

void StartOutput(std::function<void(float*, uint32_t)> render) {
    std::thread([render] {
        audioPortParam params;
        std::memset(&params, 0, sizeof params);
        params.numChannels = AUDIO_PORT_2CH;
        params.numBlocks = AUDIO_BLOCK_16;
        uint32_t port = 0;
        audioPortConfig config;
        if (audioInit() || audioPortOpen(&params, &port) || audioGetPortConfig(port, &config) || audioPortStart(port)) {
            LOGE("ps3: no audio port, the sound is dropped");
            std::vector<float> b(kRate / 100 * 2);
            for (;;) {
                render(b.data(), kRate / 100);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        LOGI("ps3: audio port %u, %llu blocks", port, (unsigned long long)config.numBlocks);
        const uint64_t blocks = config.numBlocks;
        float* data = reinterpret_cast<float*>(uintptr_t(config.audioDataStart));
        volatile uint64_t* read = reinterpret_cast<volatile uint64_t*>(uintptr_t(config.readIndex));
        constexpr uint64_t kAhead = 4;
        uint64_t next = (*read + 1) % blocks;
        for (;;) {
            uint64_t target = (*read + kAhead) % blocks;
            while (next != target) {
                render(data + next * AUDIO_BLOCK_SAMPLES * 2, AUDIO_BLOCK_SAMPLES);
                next = (next + 1) % blocks;
            }
            usleep(2000);
        }
    }).detach();
}

}  // namespace audio
