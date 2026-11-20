#!/bin/bash
# build.sh: SpiderMan2Xbox.self (and its .pkg) for the PS3 from generated/game, the code xbe_recomp made from the
# player's own default.xbe. Needs ps3toolchain (GCC 13.2) with PSL1GHT in $PS3DEV (/usr/local/ps3dev).
#   env: JOBS (nproc), OUT (build-ps3), PKG=1 also makes the .pkg, RSXGL (RSXGL's ppu folder: include/, lib/)
# The GPU renderer is gl/ (the Switch's) on RSXGL, the PS3's OpenGL 3.1 subset, patched as ps3/README.md says.
# The PS3 is big-endian: XBE_BIG_ENDIAN keeps guest memory in the console's order (sdk/runtime/core/le.h). The
# toolchain's libstdc++ has no threads and no C99 parts: ps3/compat adds them (gthreads on lv2, ps3_std.h).
# The .self holds code derived from the game: it is for the person who owns that copy, never to be shared.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
export PS3DEV=${PS3DEV:-/usr/local/ps3dev}
export PSL1GHT=${PSL1GHT:-$PS3DEV}
export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH
GEN=$R/generated/game
OUT=${OUT:-$R/build-ps3}
JOBS=${JOBS:-$(nproc)}
RSXGL=${RSXGL:-$HOME/ps3dev/rsxgl-inst/usr/local/ps3dev/ppu}
[ -f "$RSXGL/lib/libGL.a" ] || { echo "no RSXGL in $RSXGL (set RSXGL)"; exit 1; }
CXX=powerpc64-ps3-elf-g++
ARCH="-mcpu=cell -mhard-float -ffunction-sections -fdata-sections"
DEFS="-DXBE_PS3=1 -DXBE_BIG_ENDIAN=1 -DXBE_SEGMENTED=1 -DXBE_RAW_VERTICES=0 -DXBE_RENDER_THREAD=0 -DXBE_RSXGL=1 -D_GLIBCXX_HAS_GTHREADS=1 -D_GLIBCXX_HAVE_TLS=1"
# ps3/compat first: its bits/gthr-default.h takes the toolchain's place
BASE="-std=gnu++20 $ARCH $DEFS -fno-strict-aliasing -I$R/ps3/compat -include $R/ps3/compat/ps3_std.h"
INC="-I$R/sdk/runtime -I$GEN -I$PSL1GHT/ppu/include -I$PS3DEV/portlibs/ppu/include"
[ -f "$GEN/x86_func_mapping.cpp" ] || { echo "no recompiled code in $GEN: run xbe_recomp on your default.xbe first"; exit 1; }
mkdir -p "$OUT/gen" "$OUT/host"

# recompiled code: each partition rebuilt when it, or the memory macros, are newer than its object
export CXX BASE INC GEN OUT R
[ -n "$SKIP_GEN" ] || ls "$GEN"/x86_recomp.*.cpp "$GEN"/x86_func_mapping.cpp | xargs -P "$JOBS" -I{} sh -c '
    o="$OUT/gen/$(basename {} .cpp).o"
    [ "$o" -nt {} ] && [ "$o" -nt "$R/sdk/runtime/cpu/x86_ops.h" ] && [ "$o" -nt "$R/ps3/compat/ps3_std.h" ] && exit 0
    $CXX $BASE -O2 -g0 -w -DXBE_GENERATED_TU=1 $INC -c {} -o "$o.tmp" && mv "$o.tmp" "$o" || { echo "FAILED {}"; exit 1; }'

# host side: the SDK runtime without its Windows parts, the game's hooks, the harness CPU renderer, the PS3 files
SRC=$(ls "$R"/sdk/runtime/*/*.cpp | grep -Ev "/runtime/app/|/core/input.cpp|/gpu/d3d12.cpp|/audio/xaudio2.cpp")
SRC="$SRC $(ls "$R"/src/*/*.cpp) $R/sdk/harness/reference_renderer.cpp $(ls "$R"/gl/*.cpp) $(ls "$R"/ps3/*.cpp)"
HOSTINC="$INC -I$R/src -iquote $R -I$R/sdk/harness -I$R/sdk/harness/compat -I$R/android/third_party -I$RSXGL/include -idirafter $PS3DEV"
newest=$(find "$R/sdk/runtime" "$R/src" "$R/ps3" "$R/gl" "$R/sdk/harness" "$GEN/image_symbols.h" -name '*.h' -printf '%T@ %p\n' | sort -n | tail -1 | cut -d' ' -f2)
objs=()
for f in $SRC; do
    o=$OUT/host/$(echo "${f#$R/}" | tr / _ | sed 's/\.cpp$/.o/'); objs+=("$o")
    [ "$o" -nt "$f" ] && [ "$o" -nt "$newest" ] && continue
    echo "compile ${f#$R/}"
    $CXX $BASE -O2 -g -mminimal-toc $HOSTINC -c "$f" -o "$o" &  # RSXGL + Mesa fill most of the 64 KB TOC
    [ "$(jobs -r | wc -l)" -ge "$JOBS" ] && wait -n
done
wait
for o in "${objs[@]}"; do [ -f "$o" ] || { echo "missing $o"; exit 1; }; done

[ -n "$NO_LINK" ] && exit 0
echo "link"
$CXX $ARCH -Wl,--gc-sections -Wl,-Map,"$OUT/SpiderMan2Xbox.map" -o "$OUT/SpiderMan2Xbox.elf" "${objs[@]}" "$OUT"/gen/*.o \
    -L$RSXGL/lib -L$PSL1GHT/ppu/lib -L$PS3DEV/portlibs/ppu/lib \
    -Wl,--start-group -lGL -lEGL -lrsx -lgcm_sys -lio -laudio -lsysutil -lnet -lsysmodule -lrt -llv2 -lm -Wl,--end-group
ppu-strip "$OUT/SpiderMan2Xbox.elf" -o "$OUT/SpiderMan2Xbox.stripped.elf"
sprxlinker "$OUT/SpiderMan2Xbox.stripped.elf"
make_self "$OUT/SpiderMan2Xbox.stripped.elf" "$OUT/SpiderMan2Xbox.self"
ls -l "$OUT/SpiderMan2Xbox.self"
if [ -n "$PKG" ]; then
    APPID=SM2X00001
    CONTENTID=UP0001-${APPID}_00-0000000000000000
    rm -rf "$OUT/pkg" && mkdir -p "$OUT/pkg/USRDIR"
    cp "$PS3DEV/bin/ICON0.PNG" "$OUT/pkg/ICON0.PNG"
    make_self_npdrm "$OUT/SpiderMan2Xbox.stripped.elf" "$OUT/pkg/USRDIR/EBOOT.BIN" $CONTENTID > /dev/null
    sfo --title "Spider-Man 2 (Xbox)" --appid $APPID -f "$PS3DEV/bin/sfo.xml" "$OUT/pkg/PARAM.SFO"
    pkg --contentid $CONTENTID "$OUT/pkg/" "$OUT/SpiderMan2Xbox.pkg" > /dev/null
    ls -l "$OUT/SpiderMan2Xbox.pkg"
fi
