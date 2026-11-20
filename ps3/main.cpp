// PS3 entry: the port's folder, the log, the TV, the pads, the game, then the kernel boots the image and this thread
// only flushes the log and answers the system menu.
// The port's folder is /dev_hdd0/game/SM2X00001/USRDIR (logs, hdd, save). The game is looked for in its game
// folder, then on a USB stick (/dev_usb00N/SM2), as a disc image or the game's files; the first argument, when
// there is one, names another place.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include <malloc.h>
#include <ppu-lv2.h>
#include <sys/process.h>
#include <sysutil/sysutil.h>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"
#include "image_symbols.h"
#include "kernel/game_source.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "ps3/screen.h"

SYS_PROCESS_PARAM(1001, 0x100000)

namespace ps3 {
std::filesystem::path g_dir = "/dev_hdd0/game/SM2X00001/USRDIR";
}

namespace {

volatile bool g_exit = false;

void SystemEvent(u64 status, u64, void*) {
    if (status == SYSUTIL_EXIT_GAME) g_exit = true;
}

}  // namespace

int main(int argc, const char** argv) {
    std::error_code ec;
    std::filesystem::create_directories(ps3::g_dir / "logs", ec);
    logx::Init((ps3::g_dir / "logs").string(), app::kGame.log);
    LOGI("ps3: Spider-Man 2 (Xbox) for the PS3, folder %s", ps3::g_dir.string().c_str());
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, SystemEvent, nullptr);
    app::Load();
    if (app::kGame.init) app::kGame.init();
    input::Init();
    if (app::kGame.window) app::kGame.window(nullptr);

    std::vector<std::string> places;
    if (argc > 1 && argv[1] && *argv[1]) places.push_back(argv[1]);
    places.push_back((ps3::g_dir / "game").string());
    places.push_back(ps3::g_dir.string());
    for (int i = 0; i < 8; i++) places.push_back("/dev_usb00" + std::to_string(i) + "/SM2");
    std::vector<std::filesystem::path> existing;
    for (const std::string& p : places)
        if (std::filesystem::is_directory(p, ec)) existing.push_back(p);
    auto search = kernel::FindGameCopies(existing, IMAGE_XBE, "", "", {});
    auto it = std::find_if(search.copies.begin(), search.copies.end(), [](const kernel::GameCopy& c) { return c.same_entry; });
    if (it == search.copies.end()) {
        LOGE("ps3: no copy of the game: put the disc image (or the game's files) in %s/game or /dev_usb000/SM2",
             ps3::g_dir.string().c_str());
        logx::Flush();
        while (!g_exit) {
            sysUtilCheckCallback();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return 1;
    }
    LOGI("ps3: the game from %s (%s)", it->name.c_str(), it->kind.c_str());
    std::vector<uint8_t> xbe = std::move(it->xbe);
    std::string game_dir = it->folder.string();
    if (it->disc) kernel::MountDisc(std::move(it->disc));
    kernel::UseSaveFolders(it->title_id, (ps3::g_dir / "save" / "progress").string(), (ps3::g_dir / "save" / "data").string());
    if (!kernel::Boot(std::move(xbe), IMAGE_XBE, game_dir, (ps3::g_dir / "hdd").string())) {
        LOGE("ps3: the image did not boot");
        logx::Flush();
        return 1;
    }
    for (int tick = 0; !g_exit; tick++) {
        sysUtilCheckCallback();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (tick % 10 == 0) logx::Flush();
        if (tick % 50 == 0) {
            struct mallinfo mi = mallinfo();
            struct {
                uint32_t total, available;
            } user = {};
            lv2syscall1(352, uint64_t(uintptr_t(&user)));  // sys_memory_get_user_memory_size
            LOGI("ps3: memory: guest %zu MB backed, heap %d MB in use of %d MB, %u of %u MB free", mem::BackedBytes() >> 20,
                 mi.uordblks >> 20, mi.arena >> 20, user.available >> 20, user.total >> 20);
        }
    }
    LOGI("ps3: the system asked to quit");
    kernel::FlushOpenFiles();
    logx::Flush();
    std::exit(0);
}
