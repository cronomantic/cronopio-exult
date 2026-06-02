/* Cart-side bridge between Exult's file layer (files/utils.cc) and Cronopio's
 * storage. Registers Exult's <KEY> system paths (<STATIC>/<GAMEDAT>/<SAVEGAME>/
 * <PATCH>/<DATA>) onto mount prefixes and installs the istream/ostream factories:
 *   - reads  : the baked CronoFS ROM (zero-copy, romfs_cron) first, then the
 *              RAM-FS (cron_sys.c) so written files (saves) read back;
 *   - writes : the RAM-FS, which cron_sys.c persists through the host save blob.
 * Call exult_files_init() once at startup, before any U7open_* call. */
#ifndef FILES_CRON_H
#define FILES_CRON_H

/* Mount the ROM, register the system paths, and install the stream factories.
 * Idempotent. Returns the romfs_mount() code (0 = a ROM is mounted, <0 = none —
 * STATIC reads will then miss; writes still work against the RAM-FS). */
int exult_files_init(void);

#endif /* FILES_CRON_H */
