#!/usr/bin/env bash
# Build the Cronopio Exult cartridge (Exult / Ultima VII -> Cronopio .crom).
#
# BRING-UP STATUS: slice 1 (files/ + ROM-FS) is in. This script preps the
# Cronopio C++ toolchain (picolibc --with-locale + cxxio.bc), builds our generic
# CronoFS bake tool (tools/exultpak), and — given a deployed game STATIC dir —
# bakes it into a ROM and builds the files smoke cart (src/files_smoke.cc) that
# reads real game files back byte-exact through a zero-copy ROM istream. The
# imagewin 8bpp blit / audio / main-loop / input slices + the SDL3 compat/ shim
# are the next steps. See README and memory cronopio-cpp-cart-toolchain.
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

# --- 6. the cart (files smoke for now) --------------------------------------
echo "[build] building files smoke cart..."
"$CVMCC" -I "$RT" -I "$ROOT/src" -idirafter "$PICO_INC" -idirafter "$SDK/include" \
  "$ROOT/src/files_smoke.cc" "$ROOT/src/romfs_cron.cc" \
  "$ROOT/build/cron_sys.bc" "$RT/picolibc.bc" \
  "${ROM_ARG[@]+"${ROM_ARG[@]}"}" \
  --region=fb:76800:rw --region=pal:1024:rw \
  --heap-reserve=16M --stack-reserve=2M \
  -o "$ROOT/build/files_smoke.crom" || {
  echo "[build] ERROR: building the cart failed." >&2; exit 1; }

echo "[build] OK -> build/files_smoke.crom"
[[ -n "$STATIC_DIR" ]] && echo "[build] run: \"$HL\" build/files_smoke.crom 5"
