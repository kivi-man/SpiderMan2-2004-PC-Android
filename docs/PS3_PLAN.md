# Spider-Man 2 (Xbox) → PS3: tools and plan

**English** · [Türkçe](PS3_PLAN.tr.md)

Written 2026-11-18. Goal: a homebrew that runs on a PS3 with CFW or HEN and in the RPCS3 emulator, made with open
source tools only. It is a trial plan: each stage ends with a decision to go on or stop.

> [!NOTE]
> The tools' state was read straight from the GitHub API. A few points that could not be checked then, such as
> RPCS3's support for PSL1GHT homebrew, were marked "to verify" because the web search tool was not working; section 6
> has what was found since.

## 1. Chosen tools

| For | Tool | State (checked) | Why |
|---|---|---|---|
| Compiler | [ps3dev/ps3toolchain](https://github.com/ps3dev/ps3toolchain) | **GCC 13.2.0**, binutils 2.42, newlib 1.20.0. Last updated 2026-10. | Supports C++20 (the SDK needs it). PPU and SPU compilers. Builds on Linux/WSL. |
| SDK | [ps3dev/PSL1GHT](https://github.com/ps3dev/PSL1GHT) | Last updated 2026-11. `rsx/` (GPU), `sys/` (thread, mutex, cond, sem, file, memory), `audio/`, `io/` (pad), samples `samples/graphics/rsxtest*`. | The only active open source PS3 SDK. |
| GPU / shaders | [gzorin/RSXGL](https://github.com/gzorin/RSXGL) | A subset of OpenGL 3.1 core. **Compiles shaders to RSX code at run time with Mesa's GLSL 1.30 compiler.** Updated 2026-04 for RetroArch, supports the ps3dev SDK. | Solves the hardest part of the project, turning shaders into RSX code. |
| Shaders (fallback) | Mesa nouveau **nv30/nvfx** code (inside RSXGL, `extsrc/mesa`) | MIT | If RSXGL isn't enough, our own renderer uses this compiler directly. |
| Testing without a console | **RPCS3** | To verify: running PSL1GHT `.self`/`.pkg` | Most of the work happens without a PS3. |
| Big-endian test | **qemu-ppc64** + `powerpc64-linux-gnu-g++` (WSL Ubuntu) | Standard Ubuntu packages | Proves the byte order work without any graphics or PS3. The project does the same for ARM with `qemu-aarch64`. |
| Real console | CFW or PS3HEN, `ps3load` (PSL1GHT `tools/`) | — | Fast loading over the network. |

**The one closed source piece:** PSL1GHT's `cgcomp` needs NVIDIA's free but closed Cg Toolkit. RSXGL uses it only for
its own samples and optional offline compiling. The run-time GLSL path is fully open, through Mesa. The plan doesn't
depend on Cg.

> PSL1GHT has an [AI usage policy](https://github.com/ps3dev/PSL1GHT/blob/master/AI_POLICY.md). We only use the SDK,
> so it doesn't affect us. But any contribution back to PSL1GHT or RSXGL must disclose AI use, and whoever sends it
> must understand and be able to explain every line.

## 2. What the code showed

1. **The generated game code reaches memory only through macros.** In the sample file
   `generated/game/x86_recomp.10.cpp`: `X86_MEM32` 4,380 uses, `X86_MEMF32` 453, `X86_MEM8` 375, `X86_MEMF64` 14,
   `X86_MEM16` 5, and no direct `memcpy` or pointer access. So **byte swapping for 2.9 million lines of generated code
   fits in one file** (`sdk/runtime/cpu/x86_ops.h`): on a big-endian host the macros become a "reference" type that
   swaps bytes on reads and writes. PowerPC's `lwbrx`/`stwbrx` do that at no extra cost.
2. **The real audit is in the runtime** (`sdk/runtime`, ~16,300 lines): the hand-written code that reads kernel
   structures, D3D push buffers, vertex, texture and sound data. Every line that takes the console's data as
   little-endian has to be gone over.
3. **What the runtime needs from the platform:** `std::thread` (7 files), `std::filesystem` (9 files), mutexes and
   condition variables (20 files). Whether these work on the PS3 with newlib 1.20 and libstdc++ was **unknown**: the
   first thing Stage 0 tests. If not, a thin layer over PSL1GHT's `sys/thread.h` and `sys/file.h`.
4. **4 GB address space:** reserving 4 GB as on the PC and Android isn't possible. The Switch's **segmented mode**
   (`XBE_SEGMENTED`, the used parts in 1 GB blocks) exists; the PS3's 256 MB of main memory needs a smaller layout.
5. **Renderer:** `gl/gl_backend.cpp` uses OpenGL 4.3 (uniform buffers, vertex attribute binding). RSXGL is an OpenGL
   3.1 subset: **no uniform buffers, compressed textures, cube textures or copies from the framebuffer.** The Android
   layer (`gl/gl_api.cpp`: BGRA, S3TC decoded on the CPU, shader header rewrite) helps here too, but uniform buffers
   have to become plain uniforms and RSXGL's missing features have to be added.

## 3. Stages

Each stage has a measurable "done". If a stage fails, the next isn't worth starting.

### Stage 0 — Setup and first tests (1-2 days)
- Install ps3toolchain in WSL Ubuntu (the build takes a few hours) and build the PSL1GHT samples.
- Install RPCS3; run the `rsxtest` sample in it.
- **Key test:** does a small program using C++20, `std::thread`, `std::mutex`, `std::condition_variable` and
  `std::filesystem` run in RPCS3?
- Build RSXGL and run one of its samples in RPCS3.
- **Done:** the four samples run in RPCS3; libstdc++'s gaps are listed.

### Stage 1 — Big-endian (1-3 weeks) — the stage that shows whether the PS3 is possible
- `x86_ops.h`: in `XBE_BIG_ENDIAN` mode the memory macros become a byte-swapping reference type. Small changes in the
  recompiler (`sdk/tools/recomp`) if needed.
- Runtime audit: byte swapping wherever console data is read (`kernel/`, `xdk/`, `gpu/`).
- Build the Linux test build (`sdk/harness`) with `powerpc64-linux-gnu-g++` and run it headless under `qemu-ppc64` in
  WSL, with the CPU renderer (`reference_renderer.cpp`) capturing frames.
- **Done:** the game reaches the title screen and the city under qemu-ppc64; the captured frames match the PC's.

### Stage 2 — The PS3 app layer, with the CPU renderer (1-2 weeks)
- A `ps3/` folder (the Switch and Android layers' counterpart): entry point, game folder (`/dev_hdd0/game/…` or USB),
  logs, saves, PSL1GHT sound and pad, threads.
- The segmented mode's PS3 memory layout.
- For graphics first the Switch's fallback: the CPU renderer draws a frame now and then and puts it on the screen
  (slow but right).
- **Done:** the title screen shows in RPCS3, sound plays, the pad works.

### Stage 3 — GPU renderer (4-8 weeks)
- **Path A (try first): RSXGL.** Adapt the `gl/` backend to the OpenGL 3.1 / GLSL 1.30 subset (uniform buffers →
  uniforms, attribute binding → `glVertexAttribPointer`, missing formats on the CPU). Add cube textures and
  framebuffer copies to RSXGL.
- **Path B (if A gets stuck):** a thin renderer straight on PSL1GHT's `rsx` library, with shaders through Mesa's nvfx
  compiler (the code inside RSXGL) or translated directly from NV2A microcode to RSX microcode.
- Vertex and texture data are little-endian; measure the cost of swapping them on upload to the RSX.
- **Done:** the city is drawn by the GPU in RPCS3.

### Stage 4 — Real console and performance (2-4 weeks)
- Test on a PS3 with CFW/HEN through `ps3load`.
- The PPU is an in-order core: measure the hot spots. Moving sound mixing or texture decoding to the SPUs comes later.
- **Done:** a playable frame rate on the console (target 30 FPS, the game's own pace).

## 4. Risks

| Risk | Effect | Answer |
|---|---|---|
| libstdc++ has no threads/filesystem on the PS3 | Stages 0-2 take longer | A thin layer over PSL1GHT `sys/` |
| RSXGL's gaps are bigger than expected | Stage 3 takes longer | Path B |
| The PPU is too slow | Low FPS on the console | Measure, optimise hot spots, SPUs |
| 256 MB of main memory | Running out of memory | A smaller segment layout, textures kept in RSX memory |
| RPCS3 and the console behave differently | Bugs found late | Test on a console now and then from Stage 2 on |

## 5. First day

1. Check WSL Ubuntu (the project uses WSL through `build-windows.ps1`, so it may be installed).
2. Install `qemu-user-static` and `g++-powerpc64-linux-gnu`: enough for Stage 1, without waiting for ps3toolchain.
3. Start the ps3toolchain build in the background.
4. Stage 1's first step: big-endian memory macros in `x86_ops.h` and a ppc64 build of the test build.
5. Report at the end of the day: how far the game got under qemu-ppc64, which runtime parts are broken.

Estimated total: **2-4 months**. Shorter than the first estimate (4-6 months), because byte swapping fits in one file
for the generated code and RSXGL has a run-time shader compiler.

## 6. Status (2026-11-18)

- **Stage 1, first result:** `XBE_BIG_ENDIAN` mode works. The test build, built with `powerpc64-linux-gnu-g++` and run
  under `qemu-ppc64`, draws the intro logos, the movies, the title screen, name entry, loading and the city
  (Chapter 1, the rooftop) the same as on x86. How to: [BUILDING.md](BUILDING.md), "Big-endian (PowerPC)".
  - The generated code didn't change: the memory macros are byte-swapping references
    ([`x86_ops.h`](../sdk/runtime/cpu/x86_ops.h)), and the runtime reads console data through
    [`core/le.h`](../sdk/runtime/core/le.h).
  - Not yet gone over for big-endian: the `gl/` renderer, `gpu/render_thread.cpp`, `xnet` (beyond sockaddr).
- **Stage 0, the build part:** ps3toolchain (PPU GCC 13.2.0) and PSL1GHT are installed; the `blitting` sample becomes a `.self`.
  - libstdc++ has **no thread support** (`std::thread`, `std::mutex`, `std::condition_variable`): a thin layer over
    PSL1GHT `sys/` is needed.
  - `std::filesystem` compiles; linking lacks only `symlink`, `readlink`, `fchmod`, `pathconf` (empty stubs do).
  - The `rsxtest` sample wants the Cg toolkit (as expected).
- **Stage 0, RPCS3 tests** (RPCS3 0.0.42, firmware 4.93):
  - Working: C++20, exceptions (`try`/`catch`), atomics, PSL1GHT threads + mutex + condition variable (4 threads,
    400,000 counts right), `steady_clock`/`usleep`, `std::filesystem` opening folders, writing/reading/sizing/listing/renaming
    files. The `blitting` sample draws in RPCS3 through Vulkan.
  - Not working: `std::filesystem::remove_all` can't delete folders (newlib `remove()`): a small layer needed.
  - Memory: the largest `malloc` is 208 MB. Blocks of 256 MB, 512 MB and 1 GB can be reserved in the address space, 2 GB
    can't. So there's no flat 4 GB window: **the segmented mode is a must**.
  - Build settings: `-std=gnu++20` (newlib 1.20 hides functions like `snprintf` under strict `c++20`),
    `namespace std { using ::snprintf; }` for `std::snprintf`, newlib's `printf` doesn't know `%zu`.
- **Stage 2, first result (same day):** the `ps3/` layer is written ([ps3/README.md](../ps3/README.md)). The PS3 build
  (`SpiderMan2Xbox.self`) starts in RPCS3: logos, movies, title screen, new game and **the city** (Chapter 1) show
  correctly with the CPU renderer; no crash in a 10-minute run. Memory: guest ~66 MB, heap ~60 MB.
  - The sound port opens and is filled (not checked by ear). The pad mapping is written but not tried with a real DS3
    (the tests use a `pad.txt` script).
  - The CPU renderer draws about one frame a second in RPCS3: playability is Stage 3's job (the GPU renderer).

- **Stage 3 started (2026-11-19): RSXGL.** RSXGL (gzorin/RSXGL, 2026-04) builds in WSL with ps3toolchain.
  - No Python 2 needed: Mesa's 2012 generator scripts (GLSL builtins, gallium format tables) were adapted to Python 3,
    small local stand-ins replace `makedepend` and `xxd`, the GLES files are left empty (disabled in RSXGL).
  - **libstdc++'s iostream startup hangs before `main` in RPCS3 with this toolchain** (any program using `std::cout`).
    RSXGL had a single `std::cout`, now `printf`. The game's code must not use iostream.
  - A test program runs in RPCS3: EGL, a 1280x720 surface, `GL_VERSION 3.1.0 RSXGL`, `NV40`. GLSL 1.30 programs like
    the game's (constants read by a computed index, `bvec`, `texture`) compile: ~0.1 s per shader.
  - Blockers found:
    1. **Uniform arrays aren't supported**: `glUniform4fv(c, 192, …)` gives `GL_INVALID_OPERATION` (RSXGL counts the
       array as one element). RSXGL needs array support for the vertex program's `c[192]`; on the fragment side NV40
       keeps constants inside the program code, so they must be written as single uniforms instead of an array.
    2. `glReadPixels` returns zeros (reading back is needed for surfaces the CPU locks).
    3. **A wait per draw:** draws normally take microseconds, but now and then one waits exactly **300 ms**; a frame of
       500 draws takes ~470 ms. An 8 MB command buffer didn't change it. The cause must be found (RSXGL's
       synchronisation or RPCS3's behaviour with this usage); if it can't be fixed, Path A's performance won't do.
  - **Update:** the 300 ms wait is fixed. Cause: libgcm's 32 KB command windows; at the end of each window RPCS3
    waits ~300 ms. With our own callback using the command buffer as one ring, 500 draws take 0.6 ms. RSXGL is patched
    for uniform arrays (`c[192]` loads now). Left: in vertex programs a constant read by a computed index reads the
    wrong constant, and `bvec` with `mix` gives black (Mesa's nvfx compiler); `glReadPixels` still returns zeros.
  - **Update 2 (2026-11-19 morning): the game runs with the GPU renderer.** The `gl/` renderer is connected to RSXGL
    (`gl/gl_rsx.cpp`, `ps3/gl_host.cpp`, details in [ps3/README.md](../ps3/README.md)). In RPCS3 the logos and movies
    run at 40-60 FPS, the title screen at 60 FPS, 3D scenes at ~20 FPS (0.2-0.3 FPS with the CPU renderer); no crash
    in a 7.5-minute run.
    - Found and fixed in RSXGL (`ps3/rsxgl/rsxgl.patch`, rebuilt from scratch by `ps3/rsxgl/build.sh`): a framebuffer
      attachment kept a reference count instead of the object's name (colour + depth never completed), depth
      textures, uniform arrays, `glReadPixels`, stopping the process on a program it couldn't translate, iostream,
      every linked program keeping its Mesa intermediate code (~1 MB) forever.
    - Memory: the dispatch table went from 39 MB to 9 MB on the PS3; `-mminimal-toc` since RSXGL + Mesa overflow the TOC.
    - **Left:** (1) some vertex programs exceed the RSX's 512-instruction limit (545); (2) some render targets give
      "Framebuffer setup failed / depth buffer doesn't fit" (the depth surface may be smaller than the colour one);
      (3) in RPCS3 the picture doesn't reach the screen (the game draws, the screen doesn't update), probably tied to
      1 and 2; (4) the tests for constants read by a computed index (`c[a0+4]`) and `bvec` with `mix` are pending.
      Reading back in RPCS3 (glReadPixels) doesn't give the real frame.
