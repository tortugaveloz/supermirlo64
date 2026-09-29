#!/bin/bash
# The host simulator: the whole game, built for x86-64 Linux with the host
# gcc, rendering through the same code the Pocket runs -- f3d/f3d_emit.c,
# MIRLO's geometry pipeline (lang/c/geom) and MRDP's bit-exact C model
# (lang/c/mrdp) -- into PPM frames. No FPGA, no cross toolchain.
#
#   ./build.sh                  -> build/sim       the game (logo, title, ...)
#   SIM_BOOT=demo ./build.sh    -> build_demo/sim  the attract-demo chain
#   SIM_GODDARD=1               the title's Mario head (Goddard) too
#   SIM_AUDIO=1                 the sound engine and the audio core's
#                               interpreter too (needs tools/build_sound_le.sh
#                               BITWIDTH=64); SIM_AUDIO_WAV=<path> records it
#
# Needs decomp/ with its assets extracted (make assets) and MIRLO/.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/.."
SM64="${SM64_DIR:-$ROOT/decomp}"
MIRLO="${MIRLO:-$ROOT/MIRLO}"
MC="$MIRLO/lang/c"
SIM_BOOT="${SIM_BOOT:-}"
SIM_AUDIO="${SIM_AUDIO:-0}"
OUT="$HERE/build${SIM_BOOT:+_$SIM_BOOT}"
[ "$SIM_AUDIO" = 1 ] && OUT="${OUT}_audio"
LINK_EXTRA=()
mkdir -p "$OUT"

INC=(
  -I"$HERE/stub_include"                 # generated/csr.h and PR/os.h stand-ins -- FIRST
  -I"$ROOT/include"                      # segments.h
  -I"$SM64/include" -I"$SM64/build/us" -I"$SM64/build/us/include"
  -I"$SM64/src" -I"$SM64"
  -I"$ROOT/f3d" -I"$ROOT/hal" -I"$ROOT/audio" -I"$MC/game" -I"$MC/geom" -I"$MC/mrdp" -I"$MC/audio" -I"$HERE"
)
DEF=(
  -DVERSION_US=1 -D_LANGUAGE_C -DNON_MATCHING=1 -DAVOID_UB=1
  -DF3DEX_GBI_2=1 -DF3DEX_GBI_SHARED=1
  -DNO_SEGMENTED_MEMORY -DPORT_FULL_GAME -DPORT_EEPROM_RAM
  -DGEOM_HOST_TEST -DGEOM_MRDP
  -DGEOM_FB_HRES=320 -DGEOM_FB_VRES=240
  -DF3D_HOST
)
DEF+=(${SIM_EXTRA_DEFS:-})
# -ffp-contract=off, -fno-strict-aliasing and signed char: as the Pocket build
# (see ../Makefile), so the two round and alias identically.
CF=(-O2 -g -w -std=gnu11 -fno-strict-aliasing -fsigned-char -m64 -fcommon -ffp-contract=off)

# ---- the decomp (as ../Makefile's SM64_C_FILES) ---------------------------------
mapfile -t SRC < <(
  ls "$SM64"/src/game/*.c "$SM64"/src/engine/*.c "$SM64"/src/audio/*.c \
     "$SM64"/src/menu/*.c "$SM64"/src/buffers/*.c \
     $( [ "${SIM_GODDARD:-}" = 1 ] && echo "$SM64"/src/goddard/*.c "$SM64"/src/goddard/dynlists/*.c ) \
     "$SM64"/levels/*/leveldata.c "$SM64"/levels/*/script.c "$SM64"/levels/*/geo.c \
     "$SM64"/lib/src/gu*.c \
     "$SM64"/actors/*.c "$SM64"/bin/*.c \
     "$SM64"/data/behavior_data.c "$SM64"/levels/scripts.c \
     "$SM64"/build/us/assets/mario_anim_data.c "$SM64"/build/us/assets/demo_data.c \
     "$SM64"/build/us/bin/*_skybox.c \
  | { if [ "$SIM_AUDIO" = 1 ]; then cat; else grep -v '/src/game/sound_init.c'; fi; }
)
# ---- our code, the pipeline, and the simulator ----------------------------------
LOCAL=(
  "$ROOT/hal/os_hal.c"
  "$ROOT/hal/os_thread_hal.c"
  "$ROOT/hal/controller.c"
  "$ROOT/hal/exec_dl_wrap.c"
  "$ROOT/hal/rsp_stubs.c"
  $( [ "${SIM_GODDARD:-}" = 1 ] || echo "$HERE/goddard_stubs.c" )
  "$HERE/sound_data_stubs.c"
  "$HERE/sound_init_stubs.c"
  $( [ "$SIM_BOOT" = demo ] && echo "$HERE/port_boot_demo.c" || echo "$ROOT/platform/port_boot.c" )
  "$ROOT/f3d/f3d_emit.c"
  "$MC/geom/geom_pipeline.c"
  "$MC/geom/geom_triangle.c"
  "$MC/geom/geom_fixed.c"
  "$MC/geom/geom_vecmath.c"
  "$MC/mrdp/mrdp.c"
  "$HERE/sim_hud.c"
  "$HERE/sim_main.c"
)

OBJS=()
compile() {  # $1 = src, $2 = tag
  local o="$OUT/$2.o"
  gcc "${CF[@]}" "${INC[@]}" "${DEF[@]}" -c "$1" -o "$o"
  OBJS+=("$o")
}

if [ "$SIM_AUDIO" = 1 ]; then
  SND="$ROOT/build/sound_le64/sound_data_le.c"
  [ -f "$SND" ] || { echo "run first: BITWIDTH=64 $ROOT/tools/build_sound_le.sh" >&2; exit 1; }
  mapfile -t LOCAL < <(printf '%s\n' "${LOCAL[@]}" | grep -v 'sound_data_stubs.c\|sound_init_stubs.c')
  LOCAL+=("$SND" "$ROOT/hal/audio_hal.c" "$ROOT/audio/abi.c")
  DEF+=(-DSIM_AUDIO_HOST -DABI_STATE_CACHE)
  # synthesis.c steps its command pointer as a u64 * (8 bytes) while abi.h's
  # Acmd holds two uintptr_t: on a 64-bit host each command's w0 overwrites
  # the previous one's w1. Give the sim the target's 8-byte packet (a shadow
  # PR/abi.h with 32-bit words) and link it non-PIE, so that every address a
  # command carries -- static audio heap, AI buffers, sound data -- fits.
  mkdir -p "$OUT/abi_inc/PR"
  sed -e 's/^\(\s*\)uintptr_t w0;/\1unsigned int w0;/' -e 's/^\(\s*\)uintptr_t w1;/\1unsigned int w1;/' \
      "$SM64/include/PR/abi.h" > "$OUT/abi_inc/PR/abi.h"
  grep -q "unsigned int w1;" "$OUT/abi_inc/PR/abi.h" || { echo "abi.h shadow: pattern not found" >&2; exit 1; }
  INC=(-I"$OUT/abi_inc" "${INC[@]}")
  CF+=(-fno-pie)
  LINK_EXTRA=(-no-pie)
fi
echo "compiling $((${#SRC[@]} + ${#LOCAL[@]})) translation units..."
n=0
for f in "${SRC[@]}"; do
  rel="${f#"$SM64"/}"; compile "$f" "sm64_${rel//\//_}"; n=$((n+1))
  (( n % 60 == 0 )) && echo "  ... $n"
done
for f in "${LOCAL[@]}"; do compile "$f" "local_$(basename "${f%.c}")"; done

echo "linking..."
[ "$SIM_BOOT" = demo ] && LINK_EXTRA+=(-Wl,--wrap=lvl_init_or_update)   # port_boot_demo.c's demo chain
gcc "${CF[@]}" "${LINK_EXTRA[@]}" -Wl,--wrap=exec_display_list -Wl,--wrap=render_hud \
    -Wl,--wrap=end_master_display_list -o "$OUT/sim" "${OBJS[@]}" -lm
echo "OK -> $OUT/sim"
