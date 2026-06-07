#!/usr/bin/env bash
# Shared definition of the gamewin_probe cart — the LIVE Exult engine cart
# (full frame loop: render + the host graphics offload + audio + input + the
# coroutine modal driver + OSK). Sourced by BOTH:
#   * build_exult.sh        — the full from-scratch build (default output)
#   * build/gw.sh (scratch) — fast iterate; reuses prebuilt picolibc / cxxio /
#                             cron_sys / exult.rom
# so the engine TU set / include flags / offload / seal NEVER drift between the
# committed build and the scratch one. (Keeping them in sync by hand is exactly
# what bit us before — the graphics offload lived only in the gitignored gw.sh.)
#
# Caller must set: ROOT EX IW SDK RT PICO_INC CVMCC, and have built
#   build/cron_sys.bc, $RT/picolibc.bc, $RT/cxxfs.bc and build/exult.rom.
# (cvm-cc auto-links cxxio.bc via --runtime-dir; cxxfs.bc = std::filesystem,
#  NOT auto-linked, so it is listed explicitly.)
#
# NOTE: ibuf8.cc is compiled from compat/ (a LINK-OVERRIDE of imagewin/ibuf8.cc)
# — the host graphics offload (cron_blt_buf opaque paint_rle + cron_blt_buf_blend
# translucency); the fork file is NOT compiled (fork-patch-policy). Re-sync
# compat/ibuf8.cc if upstream imagewin/ibuf8.cc changes.

build_gamewin_probe() {
  local out="${1:-$ROOT/build/gamewin_probe.crom}"

  # Engine core TUs (root) — exclude main/menu/editor/debug-only ones.
  local ENGINE_ROOT=(
    game.cc gamewin.cc gamerend.cc gamemap.cc gameclk.cc gamedat.cc palette.cc
    shapeid.cc effects.cc party.cc npcnear.cc npctime.cc tqueue.cc schedule.cc
    actors.cc actorio.cc monsters.cc combat.cc mouse.cc paths.cc readnpcs.cc
    dir.cc istring.cc actions.cc drag.cc menulist.cc txtscroll.cc
    cheat.cc cheat_screen.cc browser.cc
    keys.cc keyactions.cc
  )
  local ROOTS=() f
  for f in "${ENGINE_ROOT[@]}"; do ROOTS+=("$EX/$f"); done

  # Whole subsystems by glob.
  local GLOBS=(
    "$EX/objs"/*.cc
    "$EX/usecode/conversation.cc" "$EX/usecode/intrinsics.cc" "$EX/usecode/keyring.cc"
    "$EX/usecode/stackframe.cc" "$EX/usecode/ucfunction.cc"
    "$EX/usecode/ucinternal.cc" "$EX/usecode/ucmachine.cc" "$EX/usecode/ucsched.cc"
    "$EX/usecode/ucserial.cc" "$EX/usecode/ucsymtbl.cc" "$EX/usecode/useval.cc"
    "$EX/gumps"/*.cc
    "$EX/shapes"/*.cc
    "$EX/shapes/shapeinf"/*.cc
    "$EX/gamemgr"/bggame.cc "$EX/gamemgr/devgame.cc" "$EX/gamemgr/sigame.cc"
    "$EX/pathfinder"/*.cc
    "$EX/conf"/*.cc
    "$EX/flic"/*.cc
  )

  # Files layer (Exult's real I/O) + the cart seam.
  local FILES=(
    "$EX/files/utils.cc" "$EX/files/U7file.cc" "$EX/files/U7fileman.cc"
    "$EX/files/U7obj.cc" "$EX/files/Flex.cc" "$EX/files/IFF.cc"
    "$EX/files/Table.cc" "$EX/files/Flat.cc" "$EX/files/crc.cc"
    "$EX/files/msgfile.cc"
  )

  # imagewin (render) + scalers. ibuf8.cc comes from compat/ (CART, below).
  local IMGWIN=(
    "$IW/imagewin.cc" "$IW/iwin8.cc" "$IW/imagebuf.cc"
    "$IW/scale_2x.cc" "$IW/scale_2xSaI.cc" "$IW/scale_interlace.cc" "$IW/scale_bilinear.cc"
    "$IW/scale_hq2x.cc" "$IW/scale_hq3x.cc" "$IW/scale_hq4x.cc" "$IW/scale_xbr.cc" "$IW/scale_point.cc"
    "$IW/PointScaler.cpp" "$IW/BilinearScaler.cpp" "$IW/BilinearScalerInternal_2x.cpp"
    "$IW/BilinearScalerInternal_Arb.cpp" "$IW/BilinearScalerInternal_X1Y12.cpp"
    "$IW/BilinearScalerInternal_X2Y24.cpp"
  )

  # Cart seam (full-engine build: gamewin_stubs covers only what the full link
  # still misses; exult_stubs.cc is OUT — mouse.cc/items.cc are real now).
  local CART=(
    "$ROOT/src/gamewin_probe.cc" "$ROOT/src/vid_cron.cc"
    "$ROOT/src/files_cron.cc" "$ROOT/src/romfs_cron.cc"
    "$ROOT/compat/SDL_cron.cc" "$ROOT/compat/fs_posix_stubs.c"
    "$ROOT/compat/audio_cron.cc"
    "$ROOT/compat/ibuf8.cc"     # link-override of imagewin/ibuf8.cc (host offload)
  )
  [[ -f "$ROOT/compat/gamewin_stubs.cc" ]] && CART+=("$ROOT/compat/gamewin_stubs.cc")

  # Host-native audio: Exult's PURE XMI engine (parse + MT32->GM + sequence).
  # The threaded LowLevelMidiDriver + SDL AudioMixer are NOT compiled; compat/
  # audio_cron.cc drives an XMidiSequence per frame -> cron_midi_send.
  local AUDIO=(
    "$EX/audio/midi_drivers/XMidiFile.cpp"
    "$EX/audio/midi_drivers/XMidiEventList.cpp"
    "$EX/audio/midi_drivers/XMidiSequence.cpp"
  )

  "$CVMCC" \
    -j \
    --runtime-dir="$RT" \
    -I "$RT" -I "$ROOT/src" -I "$ROOT/compat" \
    -idirafter "$PICO_INC" -idirafter "$SDK/include" \
    -I "$IW" -I "$EX/headers" -I "$EX/files" -I "$EX/shapes" -I "$EX/shapes/shapeinf" \
    -I "$EX/conf" -I "$EX/gumps" -I "$EX/objs" -I "$EX/usecode" -I "$EX/gamemgr" \
    -I "$EX/pathfinder" -I "$EX/audio" -I "$EX/audio/midi_drivers" -I "$EX/flic" \
    -I "$EX/server" \
    -I "$EX/data" -I "$EX" \
    -DHAVE_CONFIG_H -DNDEBUG -include "$ROOT/compat/cronopio_prelude.h" \
    "${CART[@]}" "${ROOTS[@]}" "${GLOBS[@]}" "${FILES[@]}" "${IMGWIN[@]}" "${AUDIO[@]}" \
    "$ROOT/build/cron_sys.bc" "$RT/picolibc.bc" "$RT/cxxfs.bc" \
    --rom="$ROOT/build/exult.rom" \
    --keep-bc --seal --region=fb:76800:rw --region=pal:1024:rw \
    --heap-reserve=64M --stack-reserve=4M \
    -o "$out"
}
