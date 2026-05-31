#!/usr/bin/env bash
# Build the Cronopio Exult cartridge (Exult / Ultima VII -> Cronopio .crom).
#
# SCAFFOLD STATUS: this currently only prepares the Cronopio toolchain (so the
# CronoVM/libc++ work can proceed). The Exult engine bring-up is NOT wired yet —
# the bring-up order (files/ + ROM-FS factory redirect -> imagewin 8bpp blit ->
# Audio singleton -> main loop), the SDL3 shim (compat/) and the content
# pipeline (tools/ expack-equiv + basegame/) are the next steps. See README.
#
# Mirrors cronopio-doom/build_doom.sh and cronopio-quake/build_quake.sh: the
# nested CronoVM toolchain (cronopio-cc) + host are rebuilt incrementally so a
# stale host never links an older VM than a freshly-built cart.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# --- Cronopio SDK submodule (nested CronoVM + picolibc + TSF + libxmp) -------
CRONOPIO="$ROOT/third_party/Cronopio"
if [[ ! -f "$CRONOPIO/CMakeLists.txt" ]]; then
  echo "[build] Cronopio submodule missing — initialising..."
  git -C "$ROOT" submodule update --init --recursive third_party/Cronopio || {
    echo "[build] ERROR: could not init the Cronopio submodule." >&2; exit 1; }
fi

RT="$CRONOPIO/external/CronoVM/runtime/lib"
CRBUILD="$CRONOPIO/build"

# Always run an incremental ninja build so the toolchain + host track the
# current CronoVM (a stale host traps "unknown opcode" on a fresh cart).
if [[ ! -f "$CRBUILD/build.ninja" ]]; then
  echo "[build] configuring Cronopio SDK (one-time)..."
  cmake -S "$CRONOPIO" -B "$CRBUILD" -G Ninja || {
    echo "[build] ERROR: cmake configure of Cronopio failed." >&2; exit 1; }
fi
echo "[build] syncing Cronopio tools + host with the current VM..."
ninja -C "$CRBUILD" cronopio-cc cronopio cronopio-headless || {
  echo "[build] ERROR: building Cronopio tools failed." >&2; exit 1; }

# C library. Exult is C++ with <iostream>/<locale>, so it needs the locale
# surface (--with-locale) once the engine is wired; for now build the standard
# stdio surface so the toolchain is exercised.
echo "[build] building picolibc.bc (C library, --with-stdio)..."
bash "$RT/build_picolibc.sh" --with-stdio || {
  echo "[build] ERROR: build_picolibc.sh failed." >&2; exit 1; }

echo "[build] Cronopio toolchain ready."
echo "[build] TODO: Exult engine bring-up not wired yet (see README + src/, compat/, tools/)."
