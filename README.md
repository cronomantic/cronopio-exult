# Cronopio Exult

A port of **Exult** (the open-source **Ultima VII: The Black Gate** engine) to the
**Cronopio** fantasy console / CronoVM virtual machine — the sibling project to
[cronopio-doom](../cronopio-doom), [cronopio-quake](../cronopio-quake) and
cronopio-uqm. DOOM and Quake proved the C/3D paths; **Exult is the C++ frontier**:
it exercises the STL, `std::iostream`/`<locale>`, RTTI and exceptions at scale, and
the port doubles as a driver for hardening CronoVM's C++ support.

## Why Exult fits Cronopio

Unlike most engines, Exult composites the **whole game + UI into an 8-bit
paletted buffer** (`Image_buffer8`/`Image_window8`) with one active 256-colour
palette — exactly Cronopio's native framebuffer model. No truecolour, no
quantiser: we blit the raw 8bpp surface + palette straight to the FB. The engine
is effectively single-threaded (no coroutine retrofit), the main loop is
non-blocking, and file I/O is already factory-abstracted (`U7open_in`/`U7open_out`
+ a `<STATIC>`/`<PATCH>`/`<GAMEDAT>` path map) — so it maps onto the ROM/RAM-FS.

## Engine base

Upstream is [Exult](https://github.com/exult/exult). We track a fork at
**`cronomantic/exult`**, branch **`cronopio-port`**, where the `[cronopio]` engine
patches live (never pushed upstream). Both the engine and the Cronopio SDK are git
submodules:

- `third_party/Cronopio` — the Cronopio SDK + CronoVM toolchain.
- `third_party/exult` — the Exult engine fork (`cronopio-port`).

## Status

**Scaffolding** — skeleton only. The toolchain gate (C++ `<iostream>`/`<locale>`
via libc++ + the translator features `std::num_get` forced) is cleared in CronoVM.
Bring-up (files/ROM-FS → 8bpp blit → audio → main loop), the SDL3 shim and the
content pipeline are next.

## Content

Ultima VII (Black Gate) data is copyrighted and **not** included — supply your own
install. Exult's own data (`exult.flx`, fonts) is built/baked at cart-build time.
