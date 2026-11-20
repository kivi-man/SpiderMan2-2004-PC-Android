<div align="center">

# Spider-Man 2 (Xbox, 2004) — Windows and Android

**Unofficial native ports of the original Xbox version of *Spider-Man 2* (Treyarch, 2004) to Windows PC and Android**,
built on SantiBros' [Nintendo Switch port](https://github.com/SantiBros/SpiderMan2-2004-NX) and TGP482's
[Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp). The game's own x86 code is recompiled ahead of time to
C++, not emulated. Bring your own copy of the game.

[Türkçe](#türkçe) · [Windows](#windows) · [Android](#android) · [PS3 (experimental)](#ps3-experimental) · [Credits](#credits) · [License](#license)

</div>

> [!IMPORTANT]
> **No game files are included, and none are allowed here.** You need your own copy of *Spider-Man 2* for the
> **original Xbox** (the US release: `default.xbe` SHA-1 `853bcb253c5baea2d7e2d9b59e5888b4c87bb402`; the Windows and Android builds
> don't carry the Switch port's European edition patch yet). Every build is made from your own
> `default.xbe`, so the built `.exe` and `.apk` contain code made from the game: **never upload them**, and don't ask for
> game files in issues.

## Status

| Platform | State | Renderer | Tested on |
|---|---|---|---|
| **Windows 10/11** | Playable, title screen to gameplay | Direct3D 12 (the SDK's own) | RTX 3070 Ti |
| **Android 8+ (arm64)** | Boots to gameplay, touch controls | OpenGL ES 3.1/3.2 (the Switch port's GL renderer through a GLES layer) | Redmi Note 13 Pro+ (Mali-G610), Android emulator |
| **Nintendo Switch** | See the [original port](https://github.com/SantiBros/SpiderMan2-2004-NX) ([its README](docs/README-switch.md)) | OpenGL 4.3 (Mesa) | — |
| **PS3** | Experimental, not playable | RSXGL | qemu-ppc64 (big-endian runtime only) |

## What this adds to the Switch port

- **Windows build** with the SDK's Direct3D 12 renderer, XAudio2 sound and SDL3 controllers (root `CMakeLists.txt`).
- **Keyboard and mouse** on Windows (`src/common/kbm.cpp`): WASD, mouse camera, raw mouse input, bindings in `settings.toml`.
- **1:1 camera** for mouse and touch (`src/common/camera_assist.cpp`): while the mouse or a finger turns the camera, the
  game's stick threshold and acceleration are turned off, so the camera follows it linearly.
- **Android port** (`android/`): SDL3, an OpenGL 4.3 → OpenGL ES 3.1/3.2 layer for the GL renderer (`gl/gl_api.cpp`:
  BGRA swaps, S3TC decoding on the CPU where the GPU has none, shader header rewrite, ES 3.1 stand-ins), touch
  controls with a floating stick, `controls.ini` for camera speed and size.
- **Big-endian mode** of the runtime (`XBE_BIG_ENDIAN`) and the start of a **PS3 port** (`ps3/`, [plan](docs/PS3_PLAN.md)).

## Windows

You need: Windows 10/11, [LLVM](https://github.com/llvm/llvm-project/releases) (Clang and LLD), CMake, Ninja, Visual
Studio 2022 or its Build Tools (for the Windows SDK), and your game.

1. Put your `default.xbe` in `assets/` (it is recompiled at the first build).
2. In a Visual Studio x64 developer shell with LLVM on `PATH`:

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
   cmake --build build
   ```

   The first build downloads Zydis, toml++, Dear ImGui and SDL3, recompiles the game (128 files) and takes 10-20 minutes.
3. Make a folder with `build/SpiderMan2.exe` and an `assets/` folder beside it holding your disc image (`.iso`), and
   run the exe from there. Settings (resolution, FPS cap, vsync, keyboard and mouse) go in `settings.toml` beside it.

Keyboard and mouse: WASD move, mouse camera, Space jump (A), left click X, right click web swing (right trigger),
E (B), F (Y), left Shift (left trigger), Q/R white/black, Esc or Enter Start, Tab Back, arrows D-pad, F10 frees the mouse.
Controllers work through SDL3.

## Android

The full guide is in Turkish: [docs/ANDROID.md](docs/ANDROID.md). In short:

1. Build the Windows version once (it makes `generated/game` from your `default.xbe`).
2. Install the Android SDK, NDK 28.2.13676358 and JDK 17 or 21; write `android/local.properties` with
   `sdk.dir=<your SDK path>`.
3. In `android/`: `gradlew assembleDebug -PABIS=arm64-v8a` → `android/app/build/outputs/apk/debug/app-debug.apk`.
4. Install it, open it once, copy your disc image over USB into
   `Android/data/com.spiderman2.xbox/files/game/`, open it again.

Needs a 64-bit phone with OpenGL ES 3.1 or newer. `controls.ini` in the same folder sets the camera's sensitivity and
the controls' size and opacity.

## PS3 (experimental)

Not playable yet. The runtime has a big-endian mode, verified under `qemu-ppc64`, and `ps3/` has a PSL1GHT build with
RSXGL. See [ps3/README.md](ps3/README.md) and [docs/PS3_PLAN.md](docs/PS3_PLAN.md).

## How it was made

This fork's Windows, Android and PS3 work was written with an AI coding assistant (Claude Code), directed and
tested by the repository's owner on real hardware. The Switch port and the SDK it builds on are the work of their
authors (below).

## Türkçe

*Spider-Man 2*'nin orijinal Xbox sürümünün **Windows ve Android'e resmi olmayan portu.** SantiBros'un Switch portu ve
TGP482'nin Xbox XBE Recomp aracı üzerine kurulu. Oyunun x86 kodu önceden C++'a çevriliyor, emülasyon yok.

- **Oyun dosyası yok:** kendi orijinal Xbox kopyan gerekiyor. Derlediğin `.exe` ve `.apk` oyundan üretilen kod
  içerir; **hiçbir yere yükleme.**
- **Windows:** yukarıdaki adımlar. Klavye/fare ve kontrolcü destekli.
- **Android:** adım adım rehber [docs/ANDROID.md](docs/ANDROID.md). Dokunmatik kontroller ve kontrolcü destekli.
- **PS3:** deneysel, henüz oynanmıyor.

## Credits

- [SantiBros](https://github.com/SantiBros/SpiderMan2-2004-NX): the Nintendo Switch port this is built on, its GL
  renderer, game fixes, widescreen, languages and disc image reader.
- [TGP482](https://github.com/TGP482/XboxXBE-Recomp): Xbox XBE Recomp, the recompiler and the Xbox runtime.
- Third-party libraries: [THIRD_PARTY.md](THIRD_PARTY.md).

## License

MIT, as the projects it builds on: [LICENSE](LICENSE) (the Switch port and this fork's additions) and
[sdk/LICENSE](sdk/LICENSE) (the SDK). *Spider-Man 2*, its characters and its art belong to their owners (Marvel,
Sony Pictures, Activision and Treyarch). This project is not affiliated with or endorsed by them.
