/* Minimal Exult build-config for the Cronopio cart.
 *
 * Exult's sources do `#ifdef HAVE_CONFIG_H #include <config.h> #endif`; the
 * autotools/cmake build normally generates this. For the freestanding cart we
 * hand-roll only what the compiled TUs actually reference (string-valued build
 * macros that can't be passed cleanly through cvm-cc's Windows arg quoting), and
 * grow it as new TUs are brought up. Compile the cart with -DHAVE_CONFIG_H and
 * -I compat. The platform feature probes (HAVE_*) are intentionally omitted:
 * Exult's TUs test them only inside #ifdef branches we don't take on i386-elf. */
#ifndef CRONOPIO_EXULT_CONFIG_H
#define CRONOPIO_EXULT_CONFIG_H

/* Default data dir for Exult's own exult*.flx. On the cart all content is mounted
 * through the ROM-FS / RAM-FS, so this is only a fallback path token. */
#ifndef EXULT_DATADIR
#	define EXULT_DATADIR "data"
#endif

#endif /* CRONOPIO_EXULT_CONFIG_H */
