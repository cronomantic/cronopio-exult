/* Cart-side CronoFS reader: mounts the baked --rom blob (cron_rom()) and serves
 * each packed file as a zero-copy std::istream backed straight by ROM memory.
 * Lookup is case-insensitive (Exult/U7 expect that for DOS-uppercase names) and
 * treats '\\' as '/'. This is the read-only side of Exult's <STATIC>/<MODS>/
 * exult*.flx; writes (GAMEDAT/SAVEGAME) go to the RAM-FS, handled elsewhere. */
#ifndef ROMFS_CRON_H
#define ROMFS_CRON_H

#include <cstdint>
#include <istream>
#include <memory>
#include <string>

/* Parse + validate the ROM header. Returns 0 on success, <0 if no ROM is baked
 * (-1) or the magic/version is wrong (-2). Safe to call more than once. */
int romfs_mount(void);

uint32_t romfs_count(void);

/* Copy entry i's stored path + size out. Returns false if i is out of range. */
bool romfs_entry_name(uint32_t i, std::string &out_path, uint32_t &out_len);

bool romfs_exists(const char *path);

/* Open a packed file for reading. Returns a std::istream whose data IS the ROM
 * span (no copy); nullptr if the path is not found. */
std::unique_ptr<std::istream> romfs_open(const char *path);

#endif /* ROMFS_CRON_H */
