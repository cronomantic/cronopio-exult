/* CronoFS — the generic read-only ROM filesystem baked into a Cronopio cart's
 * --rom blob. Format shared by the host bake tool (tools/exultpak.cc) and the
 * cart-side reader (src/romfs_cron.cc). Byte-for-byte file data; the cart serves
 * each path as a zero-copy stream straight out of ROM (cron_rom()).
 *
 * Layout (all little-endian, offsets absolute from the start of the archive):
 *   [romfs_header_t]
 *   [romfs_entry_t  x count]   at header.index_off
 *   [names blob]               at header.names_off  (path strings, no NUL)
 *   [data blob]                at header.data_off   (file bytes, concatenated)
 */
#ifndef ROMFS_FMT_H
#define ROMFS_FMT_H

#include <stdint.h>

#define ROMFS_MAGIC   "CRONFS01"   /* 8 bytes, no NUL terminator stored */
#define ROMFS_VERSION 1u

typedef struct {
    char     magic[8];    /* "CRONFS01" */
    uint32_t version;     /* ROMFS_VERSION */
    uint32_t count;       /* number of entries */
    uint32_t index_off;   /* -> romfs_entry_t[count] */
    uint32_t names_off;   /* -> names blob */
    uint32_t data_off;    /* -> data blob */
    uint32_t total_size;  /* archive size in bytes */
} romfs_header_t;         /* 32 bytes */

typedef struct {
    uint32_t name_off;    /* absolute offset of the path string */
    uint32_t name_len;    /* path length (bytes, no NUL) */
    uint32_t data_off;    /* absolute offset of the file bytes */
    uint32_t data_len;    /* file size in bytes */
} romfs_entry_t;          /* 16 bytes */

/* FNV-1a 32-bit — shared by the bake tool and the cart so a read-back can be
 * checked byte-exact against what was packed. */
static inline uint32_t romfs_fnv1a(const void *p, uint32_t n) {
    const uint8_t *b = (const uint8_t *)p;
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
    return h;
}

#endif /* ROMFS_FMT_H */
