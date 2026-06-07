#!/usr/bin/env bash
# Build the Cronopio Exult cartridge (Exult / Ultima VII -> Cronopio .crom).
#
# WHAT IT BUILDS: the full from-scratch pipeline (Cronopio tools/host, picolibc
# +locale, cxxio/cxxfs, cron_sys.bc, Exult's own data via expack, the ROM bake)
# and then the LIVE Exult cart — gamewin_probe: the full engine frame loop on the
# VM with the host graphics offload, host-native audio, the native input path and
# the coroutine modal driver + OSK. The cart RECIPE (engine TU set + flags + the
# compat/ibuf8.cc offload override + --seal) lives in build_exult_cart.sh, SHARED
# with the scratch build/gw.sh (fast iterate) so the two never drift. The old
# isolated render probes (files/win/shape/map/object/text_probe) are kept as
# diagnostics behind BUILD_PROBES=1. See memory exult-render-slice2b /
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
IW="$EX/imagewin"
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
# cvm-translate/cvm-cc/cvm-dis MUST be in this list: a CronoVM submodule bump
# does NOT rebuild the embedded _cvm tools on its own, so omitting them lets a
# STALE translator silently reproduce already-fixed miscompiles (this cost a
# multi-session bug hunt — see CronoVM docs/debugging.md).
ninja -C "$CRBUILD" cronopio-cc cronopio cronopio-headless \
                    cvm-translate cvm-cc cvm-dis || {
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

# --- 4b. Exult's OWN data (exult.flx/exult_bg.flx/... + the generated *_flx.h) ---
# Build Exult's in-tree `expack` (host) and run its data-build chain — the REAL
# Exult tooling (user's choice), which emits both the .flx archives AND the
# data/<name>_flx.h member-index headers that game.cc/shapeid.cc/bggame.cc
# #include. expack only needs files/libu7file (no SDL). The .in listings name
# their output relative to the listing's dir, and member entries likewise, so
# each invocation runs with cwd = exult/data. Generated files are mostly
# upstream-gitignored (don't dirty the fork); we treat them as build artifacts.
# Order matters: fonts + shortcutbar before flx.in (it embeds them); the bg
# paperdol/mr_faces/introsfx before bg/flx.in.
echo "[build] building expack (Exult's host data packer)..."
"$CLANGXX" -std=c++17 -O2 -DHAVE_CONFIG_H -DNDEBUG \
  -I "$EX/files" -I "$EX/headers" -I "$EX/conf" -I "$ROOT/compat" -I "$EX" \
  "$EX/tools/expack.cc" \
  "$EX/files/Flex.cc" "$EX/files/U7file.cc" "$EX/files/U7fileman.cc" "$EX/files/U7obj.cc" \
  "$EX/files/crc.cc" "$EX/files/Table.cc" "$EX/files/IFF.cc" "$EX/files/Flat.cc" \
  "$EX/files/utils.cc" "$EX/files/listfiles.cc" \
  -o "$ROOT/build/expack.exe" || {
  echo "[build] ERROR: building expack failed." >&2; exit 1; }

echo "[build] generating Exult data (exult*.flx + data/*_flx.h) via expack..."
EXPACK="$ROOT/build/expack.exe"
( cd "$EX/data" && for lst in \
    fonts/original.in fonts/serif.in shortcutbar.in flx.in \
    bg/bg_paperdol.in bg/bg_mr_faces.in bg/introsfx_mt.in bg/introsfx_sb.in bg/flx.in \
    si/flx.in ; do
    "$EXPACK" -i "$lst" >/dev/null 2>&1 || { echo "[build] ERROR: expack -i $lst failed." >&2; exit 1; }
  done ) || exit 1
[[ -f "$EX/data/exult_flx.h" && -f "$EX/data/exult_bg_flx.h" ]] || {
  echo "[build] ERROR: expack did not produce the *_flx.h headers." >&2; exit 1; }
echo "[build] OK -> Exult data + headers generated"

# --- 5. optional content bake -----------------------------------------------
ROM_ARG=()
STATIC_DIR="${1:-}"
if [[ -n "$STATIC_DIR" ]]; then
  if [[ ! -d "$STATIC_DIR" ]]; then
    echo "[build] ERROR: STATIC_DIR '$STATIC_DIR' is not a directory." >&2; exit 1; fi
  # Bake the user's read-only <STATIC> game tree AND Exult's own <DATA> extras
  # (exult.flx + the BG gameflx exult_bg.flx, generated in §4b) into ONE ROM:
  # <STATIC>/NAME -> "static/NAME", <DATA>/exult.flx -> "data/exult.flx", etc.
  # (files_cron.cc maps <STATIC>-><"static">, <DATA>-><"data">.) The .flx are
  # baked as single files (no staging copy of the big STATIC tree).
  # Digital-music pack (Exult's exult_audio.zip): bake its music/*.ogg under
  # <MUSIC> ("music/NNbg.ogg") so the cart streams them via cron_ogg_play (the
  # selectable digital-music path; files_cron maps <MUSIC>-><"music">). The pack
  # is user-provided in assets/ (like the STATIC game tree + WADs); extracted once
  # to build/audiopack/music. Absent pack -> MIDI synth only (no music mount).
  MUSIC_MOUNT=()
  AUDIO_ZIP="$ROOT/assets/exult_audio.zip"
  MUSIC_STAGE="$ROOT/build/audiopack/music"
  if [[ -f "$AUDIO_ZIP" ]]; then
    if [[ ! -d "$MUSIC_STAGE" || -z "$(ls -A "$MUSIC_STAGE" 2>/dev/null)" ]]; then
      echo "[build] extracting digital music from $(basename "$AUDIO_ZIP")..."
      mkdir -p "$MUSIC_STAGE"
      ( cd "$MUSIC_STAGE" && unzip -j -o -q "$AUDIO_ZIP" "music/*.ogg" ) || {
        echo "[build] ERROR: extracting music from audio pack failed." >&2; exit 1; }
    fi
    # Loudness-normalise the pack. The official 2002 exult_audio.zip has wildly
    # inconsistent track levels (e.g. 06bg "Trinsic" sits at RMS -55 dB = inaudible
    # while 35bg "Fellowship" is -25 dB), so some tracks seem not to play at all.
    # Pass each ogg through ffmpeg loudnorm (EBU R128, I=-18 LUFS, TP=-1.5) so every
    # track lands at a consistent, audible level. One-time (sentinel-guarded), run
    # in parallel; skipped (with a warning) if ffmpeg is unavailable.
    NORM_DONE="$MUSIC_STAGE/.loudnorm-done"
    if [[ ! -f "$NORM_DONE" ]]; then
      if command -v ffmpeg >/dev/null 2>&1; then
        echo "[build] loudness-normalising $(ls -1 "$MUSIC_STAGE"/*.ogg | wc -l) music tracks (ffmpeg loudnorm I=-18)..."
        ls "$MUSIC_STAGE"/*.ogg | xargs -P 6 -I{} bash -c '
          f="$1"; t="$f.norm.ogg"
          if ffmpeg -hide_banner -loglevel error -y -i "$f" \
               -af loudnorm=I=-18:TP=-1.5:LRA=11 -ar 22050 -ac 2 \
               -c:a libvorbis -q:a 5 "$t"; then mv -f "$t" "$f"; else rm -f "$t"; echo "[build] WARN: loudnorm failed for $f (left as-is)" >&2; fi
          ' _ {}
        touch "$NORM_DONE"
        echo "[build] music normalised."
      else
        echo "[build] WARNING: ffmpeg not found — music left un-normalised (some tracks may be near-silent)." >&2
      fi
    fi
    MUSIC_MOUNT=("$MUSIC_STAGE" music)
    echo "[build] digital music: $(ls -1 "$MUSIC_STAGE"/*.ogg 2>/dev/null | wc -l) ogg tracks -> <MUSIC>"
  fi
  # Digital SFX (the same pack's jmsfx.flx — a FLEX of 16-bit WAV sound effects)
  # baked under <DATA>/jmsfx.flx; cron_audio reads WAV member `num`, downsamples to
  # 8-bit and triggers a host SPU voice (cron_sample + cron_pcm).
  SFX_MOUNT=()
  SFX_STAGE="$ROOT/build/audiopack/jmsfx.flx"
  if [[ -f "$AUDIO_ZIP" ]]; then
    if [[ ! -f "$SFX_STAGE" ]]; then
      echo "[build] extracting digital SFX (jmsfx.flx) from $(basename "$AUDIO_ZIP")..."
      ( cd "$ROOT/build/audiopack" && unzip -o -q "$AUDIO_ZIP" "jmsfx.flx" ) || {
        echo "[build] ERROR: extracting jmsfx.flx from audio pack failed." >&2; exit 1; }
    fi
    SFX_MOUNT=("$SFX_STAGE" data)
    echo "[build] digital SFX: jmsfx.flx -> <DATA>"
  fi
  echo "[build] baking '$STATIC_DIR' (<STATIC>) + exult*.flx + sfx (<DATA>) + music (<MUSIC>) -> build/exult.rom..."
  "$ROOT/build/exultpak.exe" "$ROOT/build/exult.rom" \
    "$STATIC_DIR" static \
    "$EX/data/exult.flx" data \
    "$EX/data/exult_bg.flx" data \
    "${SFX_MOUNT[@]+"${SFX_MOUNT[@]}"}" \
    "${MUSIC_MOUNT[@]+"${MUSIC_MOUNT[@]}"}" \
    2> "$ROOT/build/manifest.txt" || {
    echo "[build] ERROR: exultpak bake failed." >&2; exit 1; }
  tail -1 "$ROOT/build/manifest.txt"
  ROM_ARG=(--rom="$ROOT/build/exult.rom")
fi

# --- 6. the LIVE Exult cart (full engine frame loop + graphics offload) ------
# gamewin_probe IS the cart (our exult.cc): the full Exult engine on the VM with
# the host graphics offload (compat/ibuf8.cc -> cron_blt_buf / cron_blt_buf_blend),
# host-native audio, the native input path, and the coroutine modal driver + OSK.
# Its recipe (engine TU set + include flags + the ibuf8 offload override + --seal)
# lives in build_exult_cart.sh, SHARED with the scratch build/gw.sh so the two
# never drift. Needs the baked ROM (game content) -> requires a STATIC_DIR.
source "$ROOT/build_exult_cart.sh"
if [[ -n "$STATIC_DIR" ]]; then
  echo "[build] building gamewin_probe cart (full Exult engine + host graphics offload)..."
  build_gamewin_probe "$ROOT/build/gamewin_probe.crom" || {
    echo "[build] ERROR: building gamewin_probe failed." >&2; exit 1; }
  echo "[build] OK -> build/gamewin_probe.crom"
  echo "[build] run: \"$HL\" build/gamewin_probe.crom 600   (the live Exult cart)"
else
  echo "[build] (no STATIC_DIR -> skipping gamewin_probe; the live cart needs baked game content)"
fi

# === legacy isolated render probes (bring-up diagnostics) ====================
# The old slice probes (files/win/shape/map/object/text_probe) are SUPERSEDED by
# gamewin_probe above; kept only as isolated render tests. Off by default — set
# BUILD_PROBES=1 to build them.
if [[ "${BUILD_PROBES:-}" == 1 ]]; then

# --- 6L. the cart (slice 1 files probe) -------------------------------------
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
  local main_src="$1" out="$2"; shift 2; local extra=("$@")
  "$CVMCC" \
    -I "$RT" -I "$ROOT/src" -I "$ROOT/compat" \
    -idirafter "$PICO_INC" -idirafter "$SDK/include" \
    -I "$IW" -I "$EX/headers" -I "$EX/files" -I "$EX/shapes" -I "$EX/shapes/shapeinf" \
    -I "$EX/conf" -I "$EX/gumps" -I "$EX/data" -I "$EX" \
    -DHAVE_CONFIG_H -DNDEBUG -include "$ROOT/compat/cronopio_prelude.h" \
    "$main_src" "${extra[@]+"${extra[@]}"}" "$ROOT/src/vid_cron.cc" \
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

  echo "[build] building text_probe cart (real U7 fonts via Exult Font -> CRON_FB, render slice text)..."
  build_render_cart "$ROOT/src/text_probe.cc" "$ROOT/build/text_probe.crom" "$EX/shapes/font.cc" || {
    echo "[build] ERROR: building text_probe failed." >&2; exit 1; }
  echo "[build] OK -> build/text_probe.crom  (run: \"$HL\" build/text_probe.crom 5 --ppm build/text_shot.ppm  -> real U7 text lines in several fonts)"
fi

fi  # end: legacy isolated render probes (BUILD_PROBES=1)
