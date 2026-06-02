/* Freestanding prelude force-included (cvm-cc -include) before each Exult TU.
 *
 * Exult's own headers assume some standard headers are already visible through
 * transitive includes that a hosted libc++/SDL build happens to pull in but our
 * freestanding libc++ does not (e.g. istring.h uses std::tolower without
 * including <cctype>). Rather than patch the engine source (see memory
 * fork-patch-policy), we force-include the missing standard headers here. Keep
 * this to STANDARD headers only — it is a toolchain prelude, not engine glue.
 * Add entries as new TUs reveal more transitively-assumed headers. */
#ifndef CRONOPIO_PRELUDE_H
#define CRONOPIO_PRELUDE_H

/* cvm-cc force-includes this before EVERY TU it compiles, including the C
 * runtime TUs (cvm_float64_rt.c, …) which have no libc++ on their search path —
 * so guard the C++ standard headers behind __cplusplus (a no-op in C). */
#ifdef __cplusplus
#	include <cctype> /* std::tolower/std::toupper — istring.h assumes it */
#endif

#endif /* CRONOPIO_PRELUDE_H */
