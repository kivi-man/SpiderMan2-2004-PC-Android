# PS3 build

A homebrew `.self` (and `.pkg`) for a PS3 with CFW or HEN, and for RPCS3. The picture comes from the GPU renderer
the Switch uses (`gl/`), on [RSXGL](https://github.com/gzorin/RSXGL), the PS3's OpenGL 3.1 subset, with this port's
fixes to it. When it does not start, the CPU renderer draws instead (a frame now and then, the game at its speed).
Status and plan: [docs/PS3_PLAN.md](../docs/PS3_PLAN.md).

## Build (Linux or WSL)

Needs [ps3toolchain](https://github.com/ps3dev/ps3toolchain) with PSL1GHT in `$PS3DEV` (`/usr/local/ps3dev`), RSXGL
built by `ps3/rsxgl/build.sh`, and the recompiled code in `generated/game`.

```sh
bash ps3/rsxgl/build.sh      # once: RSXGL with rsxgl.patch, into ~/ps3dev/rsxgl-inst
bash ps3/build.sh            # build-ps3/SpiderMan2Xbox.self
PKG=1 bash ps3/build.sh      # also build-ps3/SpiderMan2Xbox.pkg (title ID SM2X00001)
```

`RSXGL=<folder>` points `ps3/build.sh` at another RSXGL (its `ppu` folder, with `include/` and `lib/`).

## Run

The port keeps its files in `/dev_hdd0/game/SM2X00001/USRDIR`: `logs`, `hdd`, `save`, `shaders.cache`. The game
(the disc image or its files) goes in `USRDIR/game`, or on a USB stick in `SM2` (`/dev_usb000/SM2`). In RPCS3 that
folder is `dev_hdd0/game/SM2X00001/USRDIR` beside `rpcs3.exe`; boot the `.self` with File > Boot or
`rpcs3 --no-gui <self>`. The first start of a new build spends a minute or two compiling the PPU code, and RPCS3's
own shader precompile ("Precompiling shader interpreter variants") hides the picture for several minutes; both are
kept for the next start of that build.

Controls: cross A, circle B, square X, triangle Y, L1 white, R1 black, L2 and R2 the triggers, select back.
`pad.txt` in `USRDIR`: presses from a script for tests, lines of `seconds buttons [seconds held]`.

## How it fits the PS3

- Big-endian: `XBE_BIG_ENDIAN` keeps guest memory in the console's order (`sdk/runtime/core/le.h`). The GPU reads
  the host's order: vertices are converted on the CPU, the title's 16-bit indices swapped (`gl/gl_backend.cpp`).
- 256 MB: the guest's address space is lv2 address areas, backed in 1 MB pages as the game commits memory
  (`sdk/runtime/kernel/memory.cpp`); about 70 MB for the guest and 60 MB of heap in the city. RSXGL keeps buffers
  and textures in the RSX's own 256 MB; 8 MB of main memory holds its command buffer.
- The toolchain's libstdc++ has no threads and no C99 parts: `compat/bits/gthr-default.h` puts its threads on lv2
  lightweight mutexes, semaphores and PPU threads (`gthr.cpp`), `compat/ps3_std.h` adds the C99 functions and
  turns newlib's missing `%zu` into `%lu`. Its iostream start-up hangs before `main` in RPCS3: no `std::cout`.

## The GPU renderer on RSXGL

- `gl/gl_rsx.cpp` (`XBE_RSXGL`): the renderer's OpenGL 4.3 calls on RSXGL's 3.1. GLSL 4.30 is rewritten to 1.30
  (uniform blocks to plain uniforms sent when they change, interface blocks to varyings, `layout()` set by name,
  `textureSize`/`texelFetch` through a size uniform, integer bit tricks and `round`/`ceil` to float arithmetic,
  `gl_VertexID` to an attribute); vertex buffer bindings become `glVertexAttribPointer` at each draw; S3TC is decoded
  on the CPU.
- `gl_host.cpp`: the EGL context, and the command buffer as one ring. libgcm's own callback hands out 32 KB windows
  and waits at the end of each (in RPCS3 ~300 ms, every few hundred draws); the ring jumps back to its start only when
  the whole buffer is used.
- `gl_procs.cpp`: RSXGL's functions by name (its `eglGetProcAddress` searches an unsorted table).
- `rsxgl/rsxgl.patch`: what RSXGL needed (`rsxgl/build.sh` lists it).
