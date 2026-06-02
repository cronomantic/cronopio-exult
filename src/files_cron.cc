/* See files_cron.h. Wires Exult's U7open_* factory hooks to Cronopio storage. */
#include "files_cron.h"

#include "listfiles.h"    /* Exult: U7ListFiles / FileList */
#include "romfs_cron.h"
#include "utils.h"        /* Exult: add_system_path, U7set_*_factory */

#include <fstream>
#include <ios>
#include <istream>
#include <memory>
#include <ostream>
#include <string>

/* Mount prefixes the factories see AFTER get_system_path() expands a <KEY>.
 * <STATIC> matches the prefix exultpak bakes into the ROM ("static/NAME"); the
 * writable trees live in the cron_sys.c RAM-FS under their own prefixes. */
int exult_files_init(void) {
    int rc = romfs_mount();

    add_system_path("<STATIC>", "static");
    add_system_path("<GAMEDAT>", "gamedat");
    add_system_path("<SAVEGAME>", "savegame");
    add_system_path("<PATCH>", "patch");
    add_system_path("<DATA>", "data");

    /* Reads: ROM (read-only game content) first, then the RAM-FS so files we
     * wrote (saves, gamedat) read back. romfs_open is case-insensitive, which
     * is what Exult expects for the DOS-uppercase content names. */
    U7set_istream_factory(
            [](const char* s, std::ios_base::openmode mode)
                    -> std::unique_ptr<std::istream> {
                if (auto in = romfs_open(s)) {
                    return in;
                }
                return std::make_unique<std::ifstream>(s, mode);
            });

    /* Writes: the RAM-FS (cron_sys.c persists it through the host save blob). */
    U7set_ostream_factory(
            [](const char* s, std::ios_base::openmode mode)
                    -> std::unique_ptr<std::ostream> {
                return std::make_unique<std::ofstream>(s, mode);
            });

    return rc;
}

/* Directory enumeration is not wired in slice 1 (read/write factories only). The
 * real U7ListFiles (files/listfiles.cc) globs the host FS; the cart will instead
 * enumerate the ROM-FS (romfs_count/romfs_entry_name) + the RAM-FS (opendir/
 * readdir, both provided by cron_sys.c) in the directory-listing slice. Until
 * then provide an empty result so utils.cc's U7rmdir/setup paths link (none are
 * on the read/write critical path the probe exercises). Returns 0 = no matches. */
int U7ListFiles(const std::string& mask, FileList& files, bool quiet) {
    (void)mask;
    (void)files;
    (void)quiet;
    return 0;
}
