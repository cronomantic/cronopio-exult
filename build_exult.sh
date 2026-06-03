#!/usr/bin/env bash
# Build the Cronopio Exult cartridge (Exult / Ultima VII -> Cronopio .crom).
#
# BRING-UP STATUS: slice 1 (files/ + ROM-FS) is in — Exult's REAL file layer
# (files/utils.cc) now runs on the VM, redirected to Cronopio storage. This
# script preps the Cronopio C++ toolchain (picolibc --with-locale + cxxio.bc),
# builds our generic CronoFS bake tool (tools/exultpak), and — given a deployed
# game STATIC dir — bakes it into a ROM and builds the files probe cart
# (src/files_probe.cc) that reads real game files back byte-exact through Exult's
# own U7open_in and round-trips a GAMEDAT write through the RAM-FS. The imagewin
# 8bpp blit / audio / main-loop / input slices + the SDL3 compat/ shim (incl.
# SDL_IOStream, the 2nd I/O path) are the next steps. See README + memory
# cronopio-cpp-cart-toolchain / exult-port-scout.
#
# Usage:  build_exult.sh [STATIC_DIR]
#   STATIC_DIR : a deployed game data dir (e.g. ".../Ultima 7/STATIC"). If given,
#                it is baked (READ-ONLY) into build/exult.rom and the smoke cart
#                links it; if omitted, the cart builds with no ROM.
#
# C++ carts are driven through cvm-cc DIRECTLY (not cronopio-cc, which force-adds
# the SDK include and shadows picolibc's headers). See cronopio-cpp-cart-toolchain.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CRONOPIO="$ROOT/third_party/Cronopio"
if [[ ! -f "$CRONOPIO/CMakeLists.txt" ]]; then
  echo "[build] Cronopio submodule missing — initialising..."
  git -C "$ROOT" submodule update --init --recursive third_party/Cronopio || {
    echo "[build] ERROR: could not init the Cronopio submodule." >&2; exit 1; }
fi

SDK="$CRONOPIO/sdk"
RT="$CRONOPIO/external/CronoVM/runtime/lib"
PICO_INC="$CRONOPIO/external/CronoVM/external/picolibc/libc/include"
EX="$ROOT/third_party/exult"   # Exult engine fork (branch cronopio-port)
CRBUILD="$CRONOPIO/build"
CVMCC="$CRBUILD/_cvm/tools/cvm-cc/cvm-cc.exe"
HL="$CRBUILD/tools/headless/cronopio-headless.exe"

# Toolchain discovery (override via env). clang for the i386-elf machine-port
# bitcode + the host bake tool; llvm-link for the runtime libs.
CLANG="${CLANG:-/c/Users/Sergio/scoop/apps/mingw-mstorsjo-llvm-ucrt/current/bin/clang}"
CLANGXX="${CLANGXX:-$(dirname "$CLANG")/clang++}"
LLVM_LINK="${LLVM_LINK:-/c/msys64/ucrt64/bin/llvm-link}"

mkdir -p "$ROOT/build"

# --- 1. Cronopio tools + host (always re-sync with the current VM) ----------
if [[ ! -f "$CRBUILD/build.ninja" ]]; then
  echo "[build] configuring Cronopio SDK (one-time)..."
  cmake -S "$CRONOPIO" -B "$CRBUILD" -G Ninja || {
    echo "[build] ERROR: cmake configure of Cronopio failed." >&2; exit 1; }
fi
echo "[build] syncing Cronopio tools + host with the current VM..."
ninja -C "$CRBUILD" cronopio-cc cronopio cronopio-headless || {
  echo "[build] ERROR: building Cronopio tools failed." >&2; exit 1; }

# --- 2. C++ runtime libs: picolibc (+locale) and the libc++ iostream lib -----
echo "[build] building picolibc.bc (--with-stdio --with-locale)..."
CLANG="$CLANG" LLVM_LINK="$LLVM_LINK" bash "$RT/build_picolibc.sh" --with-stdio --with-locale || {
  echo "[build] ERROR: build_picolibc.sh failed." >&2; exit 1; }
echo "[build] building cxxio.bc (libc++ iostream/locale)..."
CLANG="$CLANG" LLVM_LINK="$LLVM_LINK" bash "$RT/build_cxxio.sh" || {
  echo "[build] ERROR: build_cxxio.sh failed." >&2; exit 1; }

# --- 3. machine port to bitcode (SDK headers; locale-aware) ------------------
echo "[build] compiling cron_sys.bc (machine port, -DCRON_SYS_LIBC_HAS_LOCALE)..."
"$CLANG" --target=i386-elf -ffreestanding -emit-llvm -O1 -gline-tables-only \
  -DCRON_SYS_LIBC_HAS_LOCALE -I "$SDK/include" -I "$RT" \
  -c "$SDK/lib/cron_sys.c" -o "$ROOT/build/cron_sys.bc" || {
  echo "[build] ERROR: compiling cron_sys.c failed." >&2; exit 1; }

# --- 4. our CronoFS bake tool (host) ----------------------------------------
echo "[build] building exultpak (host bake tool)..."
"$CLANGXX" -std=c++17 -O2 "$ROOT/tools/exultpak.cc" -o "$ROOT/build/exultpak.exe" || {
  echo "[build] ERROR: building exultpak failed." >&2; exit 1; }

# --- 5. optional content bake -----------------------------------------------
ROM_ARG=()
STATIC_DIR="${1:-}"
if [[ -n "$STATIC_DIR" ]]; then
  if [[ ! -d "$STATIC_DIR" ]]; then
    echo "[build] ERROR: STATIC_DIR '$STATIC_DIR' is not a directory." >&2; exit 1; fi
  echo "[build] baking '$STATIC_DIR' -> build/exult.rom (read-only)..."
  "$ROOT/build/exultpak.exe" "$STATIC_DIR" static "$ROOT/build/exult.rom" 2> "$ROOT/build/manifest.txt" || {
    echo "[build] ERROR: exultpak bake failed." >&2; exit 1; }
  tail -1 "$ROOT/build/manifest.txt"
  ROM_ARG=(--rom="$ROOT/build/exult.rom")
fi

# --- 6. the cart (slice 1 files probe) --------------------------------------
# Drives Exult's REAL file layer (third_party/exult/files/utils.cc) through the
# Cronopio bridge (src/files_cron.cc): the istream/ostream factories route reads
# to the ROM-FS (romfs_cron) then the RAM-FS, writes to the RAM-FS. utils.cc
# compiles unpatched against compat/config.h (-DHAVE_CONFIG_H) with asserts off
# (-DNDEBUG). Include order: the cart's src/ + compat/, then Exult's files/headers
# dirs, then (idirafter) picolibc and the SDK so libc++'s wrappers win.
echo "[build] building files probe cart (Exult file layer over ROM-FS/RAM-FS)..."
"$CVMCC" \
  -I "$RT" -I "$ROOT/src" -I "$ROOT/compat" \
  -idirafter "$PICO_INC" -idirafter "$SDK/include" \
  -I "$EX/files" -I "$EX/headers" -I "$EX" -DHAVE_CONFIG_H -DNDEBUG \
  "$ROOT/src/files_probe.cc" "$ROOT/src/files_cron.cc" "$ROOT/src/romfs_cron.cc" \
  "$EX/files/utils.cc" \
  "$ROOT/build/cron_sys.bc" "$RT/picolibc.bc" \
  "${ROM_ARG[@]+"${ROM_ARG[@]}"}" \
  --region=fb:76800:rw --region=pal:1024:rw \
  --heap-reserve=16M --stack-reserve=2M \
  -o "$ROOT/build/files_probe.crom" || {
  echo "[build] ERROR: building the cart failed." >&2; exit 1; }

echo "[build] OK -> build/files_probe.crom"
[[ -n "$STATIC_DIR" ]] && echo "[build] run: \"$HL\" build/files_probe.crom 5"

# --- 7. the render cart (slice 2b: Image_window8 -> CRON_FB) -----------------
# Brings up Exult's REAL Image_window8 (imagewin/iwin8/ibuf8 + the scaler table)
# on the VM with ZERO source patches. The SDL3 chain is shimmed in compat/
# (SDL_GetDesktopDisplayMode reports INDEX8 so create_surface composites an 8bpp
# surface); win_probe draws a test pattern with the window's own fill8/draw_line8
# and blits the engine's 8bpp buffer (get_ib8()->get_bits()) + palette to CRON_FB
# via src/vid_cron.cc — the "observe the engine buffer" present model (no
# UpdateRect patch). No ROM needed. See memory exult-render-slice2b.
IW="$EX/imagewin"
echo "[build] building win_probe cart (Image_window8 -> CRON_FB, render slice 2b)..."
"$CVMCC" \
  -I "$RT" -I "$ROOT/src" -I "$ROOT/compat" \
  -idirafter "$PICO_INC" -idirafter "$SDK/include" \
  -I "$IW" -I "$EX/headers" -I "$EX/files" -I "$EX/conf" -I "$EX/shapes" -I "$EX/gumps" -I "$EX" \
  -DHAVE_CONFIG_H -DNDEBUG -include "$ROOT/compat/cronopio_prelude.h" \
  "$ROOT/src/win_probe.cc" "$ROOT/src/vid_cron.cc" \
  "$ROOT/compat/SDL_cron.cc" "$ROOT/compat/exult_stubs.cc" \
  "$IW/imagewin.cc" "$IW/iwin8.cc" "$IW/ibuf8.cc" "$IW/imagebuf.cc" "$EX/istring.cc" \
  "$IW/scale_2x.cc" "$IW/scale_2xSaI.cc" "$IW/scale_interlace.cc" "$IW/scale_bilinear.cc" \
  "$IW/scale_hq2x.cc" "$IW/scale_hq3x.cc" "$IW/scale_hq4x.cc" "$IW/scale_xbr.cc" "$IW/scale_point.cc" \
  "$IW/PointScaler.cpp" "$IW/BilinearScaler.cpp" "$IW/BilinearScalerInternal_2x.cpp" \
  "$IW/BilinearScalerInternal_Arb.cpp" "$IW/BilinearScalerInternal_X1Y12.cpp" \
  "$IW/BilinearScalerInternal_X2Y24.cpp" \
  "$ROOT/build/cron_sys.bc" "$RT/picolibc.bc" \
  --region=fb:76800:rw --region=pal:1024:rw \
  --heap-reserve=16M --stack-reserve=2M \
  -o "$ROOT/build/win_probe.crom" || {
  echo "[build] ERROR: building win_probe failed." >&2; exit 1; }

echo "[build] OK -> build/win_probe.crom"
echo "[build] run: \"$HL\" build/win_probe.crom 5   (expect 'presented ...' + distinct colors ~17)"

# --- 8+9. content render carts (slice 3: shapes, slice 4: map region) --------
# Need the baked ROM (a STATIC_DIR), so only built when one was given. Render
# REAL Ultima 7 art end to end through Exult's OWN pipeline: the game palette
# from <STATIC>/palettes.flx + content (faces.vga portraits / u7map+u7chunks
# terrain), decoded by Exult's Vga_file/Shape_frame and composited via paint_rle/
# paint into the engine's Image_buffer8, then blitted to CRON_FB. File access
# routes through Exult's U7open_in -> the ROM (the files_cron bridge from slice 1).
# build_render_cart <main.cc> <out.crom> — same TU set, just a different probe main.
build_render_cart() {
  local main_src="$1" out="$2"
  "$CVMCC" \
    -I "$RT" -I "$ROOT/src" -I "$ROOT/compat" \
    -idirafter "$PICO_INC" -idirafter "$SDK/include" \
    -I "$IW" -I "$EX/headers" -I "$EX/files" -I "$EX/shapes" -I "$EX/conf" -I "$EX/gumps" -I "$EX" \
    -DHAVE_CONFIG_H -DNDEBUG -include "$ROOT/compat/cronopio_prelude.h" \
    "$main_src" "$ROOT/src/vid_cron.cc" \
    "$ROOT/src/files_cron.cc" "$ROOT/src/romfs_cron.cc" \
    "$ROOT/compat/SDL_cron.cc" "$ROOT/compat/exult_stubs.cc" \
    "$EX/files/utils.cc" "$EX/files/U7file.cc" "$EX/files/U7fileman.cc" \
    "$EX/files/U7obj.cc" "$EX/files/Flex.cc" "$EX/files/IFF.cc" \
    "$EX/files/Table.cc" "$EX/files/Flat.cc" "$EX/shapes/vgafile.cc" \
    "$IW/imagewin.cc" "$IW/iwin8.cc" "$IW/ibuf8.cc" "$IW/imagebuf.cc" "$EX/istring.cc" \
    "$IW/scale_2x.cc" "$IW/scale_2xSaI.cc" "$IW/scale_interlace.cc" "$IW/scale_bilinear.cc" \
    "$IW/scale_hq2x.cc" "$IW/scale_hq3x.cc" "$IW/scale_hq4x.cc" "$IW/scale_xbr.cc" "$IW/scale_point.cc" \
    "$IW/PointScaler.cpp" "$IW/BilinearScaler.cpp" "$IW/BilinearScalerInternal_2x.cpp" \
    "$IW/BilinearScalerInternal_Arb.cpp" "$IW/BilinearScalerInternal_X1Y12.cpp" \
    "$IW/BilinearScalerInternal_X2Y24.cpp" \
    "$ROOT/build/cron_sys.bc" "$RT/picolibc.bc" "${ROM_ARG[@]+"${ROM_ARG[@]}"}" \
    --region=fb:76800:rw --region=pal:1024:rw \
    --heap-reserve=16M --stack-reserve=2M \
    -o "$out"
}
if [[ -n "$STATIC_DIR" ]]; then
  echo "[build] building shape_probe cart (real U7 faces -> CRON_FB, render slice 3)..."
  build_render_cart "$ROOT/src/shape_probe.cc" "$ROOT/build/shape_probe.crom" || {
    echo "[build] ERROR: building shape_probe failed." >&2; exit 1; }
  echo "[build] OK -> build/shape_probe.crom  (run: \"$HL\" build/shape_probe.crom 5 --ppm build/shape_shot.ppm  -> 15 U7 faces, ~187 colours)"

  echo "[build] building map_probe cart (real U7 world region -> CRON_FB, render slice 4)..."
  build_render_cart "$ROOT/src/map_probe.cc" "$ROOT/build/map_probe.crom" || {
    echo "[build] ERROR: building map_probe failed." >&2; exit 1; }
  echo "[build] OK -> build/map_probe.crom  (run: \"$HL\" build/map_probe.crom 5 --ppm build/map_shot.ppm  -> U7 terrain: grass/forest/water)"

  echo "[build] building object_probe cart (real U7 ifix objects on terrain -> CRON_FB, render slice objects)..."
  build_render_cart "$ROOT/src/object_probe.cc" "$ROOT/build/object_probe.crom" || {
    echo "[build] ERROR: building object_probe failed." >&2; exit 1; }
  echo "[build] OK -> build/object_probe.crom  (run: \"$HL\" build/object_probe.crom 5 --ppm build/object_shot.ppm  -> U7 terrain + ifix objects: trees/signs/walls)"
fi
