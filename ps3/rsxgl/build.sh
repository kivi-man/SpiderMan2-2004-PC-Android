#!/bin/bash
# build.sh: RSXGL (github.com/gzorin/RSXGL), the PS3 OpenGL 3.1 library the PS3 build's GPU renderer runs on, with
# this port's fixes (rsxgl.patch), installed into $DEST/usr/local/ps3dev/ppu (ps3/build.sh's RSXGL default).
# Needs ps3toolchain + PSL1GHT ($PS3DEV), git, autotools, bison, flex, python3, rsync. No Python 2: the 2012 Mesa
# generator scripts the build runs are converted to Python 3 (py2to3.py), and makedepend and xxd get small stand ins.
#   env: WORK (~/ps3dev: the clone, the build folder), DEST (~/ps3dev/rsxgl-inst), JOBS (nproc)
#
# rsxgl.patch, on RSXGL 835ecd3 (2026-04-28):
#   - framebuffer attachments kept the object's reference count in place of its name (colour + depth never complete)
#   - depth textures: no sampler format for depth in Mesa's check, no source format for depth pixels
#   - uniform arrays (a uniform's count was its matrix columns only), arrays past their end set what fits
#   - glReadPixels did nothing; now it waits for the GPU and converts on the CPU
#   - a program nvfx cannot translate fails to link instead of aborting the process
#   - std::cout (libstdc++'s iostream start-up hangs before main on this toolchain in RPCS3) -> printf
#   - a linked program kept Mesa's linked shaders (~1 MB each: IR with the built-ins, TGSI): freed once translated,
#     with a Driver.DeleteProgram for that (its Mesa context had none)
#   - a vertex program past the RSX's 512 instructions fails to link (it was uploaded past the program memory's end)
#   - a framebuffer with only a depth attachment set no surfaces (its clears and draws went to the previous one's);
#     the depth attachment counted for the size and format only while depth writes were on
#   - rsxgl_dump_programs (a global, 0 by default): each vertex program's TGSI and NV40 microcode to stdout
set -e
H=$(cd "$(dirname "$0")" && pwd)
export PS3DEV=${PS3DEV:-/usr/local/ps3dev}
export PSL1GHT=${PSL1GHT:-$PS3DEV}
WORK=${WORK:-$HOME/ps3dev}
DEST=${DEST:-$HOME/ps3dev/rsxgl-inst}
JOBS=${JOBS:-$(nproc)}
SRC=$WORK/RSXGL
BUILD=$WORK/rsxgl-build
SHIM=$WORK/shim

if [ ! -d "$SRC" ]; then
    git clone https://github.com/gzorin/RSXGL.git "$SRC"
    git -C "$SRC" checkout 835ecd3
fi
git -C "$SRC" diff --quiet && git -C "$SRC" apply "$H/rsxgl.patch"

# makedepend: only Mesa's "make depend"; xxd -i: the samples; python2: python3 on the converted scripts
mkdir -p "$SHIM"
printf '#!/bin/sh\nexit 0\n' > "$SHIM/makedepend"
cat > "$SHIM/xxd" <<'X'
#!/usr/bin/env python3
import sys, re
a = [x for x in sys.argv[1:] if x != '-i']
d = open(a[0], 'rb').read(); n = re.sub(r'[^0-9a-zA-Z]', '_', a[0])
out = sys.stdout if len(a) < 2 else open(a[1], 'w')
out.write('unsigned char %s[] = {\n' % n)
for i in range(0, len(d), 12): out.write('  ' + ', '.join('0x%02x' % b for b in d[i:i + 12]) + (',\n' if i + 12 < len(d) else '\n'))
out.write('};\nunsigned int %s_len = %d;\n' % (n, len(d)))
X
printf '#!/bin/sh\nexec python3 "$@"\n' > "$SHIM/python2"
# -mminimal-toc: RSXGL with its Mesa needs ~70 KB of TOC entries, past the 64 KB one TOC reaches; the game's own
# code on top of that does not link otherwise. Not for gl_fifo.c: its inline assembly uses r30, minimal TOC's register.
for c in gcc g++; do
    cat > "$SHIM/ppu-$c-mtoc" <<X
#!/bin/sh
case "\$*" in *gl_fifo*) exec $PS3DEV/ppu/bin/ppu-$c "\$@" ;; esac
exec $PS3DEV/ppu/bin/ppu-$c -mminimal-toc "\$@"
X
done
chmod +x "$SHIM"/*
export PATH=$SHIM:$PATH PYTHON=$SHIM/python2

if [ ! -f "$BUILD/Makefile" ]; then
    (cd "$SRC" && autoreconf -fi)
    mkdir -p "$BUILD"
    (cd "$BUILD" && ppu_CC=$SHIM/ppu-gcc-mtoc ppu_CXX=$SHIM/ppu-g++-mtoc "$SRC/configure")
    # configure copied Mesa into the build folder: its generator scripts to Python 3, the GLES sources empty
    M=$BUILD/extsrc/mesa/src
    python3 "$H/py2to3.py" "$M"/glsl/builtins/tools/*.py \
        "$M"/gallium/auxiliary/indices/u_indices_gen.py "$M"/gallium/auxiliary/indices/u_unfilled_gen.py \
        "$M"/gallium/auxiliary/util/u_format_srgb.py "$M"/gallium/auxiliary/util/u_format_table.py \
        "$M"/gallium/auxiliary/util/u_half.py "$M"/gallium/auxiliary/util/u_format_parse.py \
        "$M"/gallium/auxiliary/util/u_format_pack.py
    for f in api_exec_es1.c api_exec_es2.c api_exec_es1_dispatch.h api_exec_es1_remap_helper.h \
             api_exec_es2_dispatch.h api_exec_es2_remap_helper.h; do
        echo "/* GLES is off in RSXGL's Mesa (FEATURE_ES1/ES2 0) */" > "$M/mesa/main/$f"
    done
fi
make -C "$BUILD" -j"$JOBS"
make -C "$BUILD" install DESTDIR="$DEST"
ls -l "$DEST/usr/local/ps3dev/ppu/lib/libGL.a" "$DEST/usr/local/ps3dev/ppu/lib/libEGL.a"
