/* Slice 1 smoke: mount the baked CronoFS ROM, enumerate it, and read real Black
 * Gate files back byte-exact through a zero-copy ROM istream. Queries use
 * lowercase paths against UPPERCASE DOS names to prove case-insensitive lookup.
 * The logged `fnv=` must match exultpak's PACKED manifest for the same file.
 * Throwaway harness for the files/ slice; the Exult factory redirect comes next. */
#include <cronopio.h>   /* extern "C" guarded */

#include "romfs_cron.h"
#include "romfs_fmt.h"

#include <memory>
#include <sstream>
#include <string>

static void logln(const std::string &s) {
    std::string l = s;
    l.push_back('\n');
    cron_log(l.data(), (int32_t)l.size());
}

static void readback(const char *path) {
    auto is = romfs_open(path);
    if (!is) { logln(std::string("MISS     ") + path); return; }
    uint32_t h = 2166136261u, n = 0;
    char buf[4096];
    while (*is) {
        is->read(buf, sizeof buf);
        std::streamsize g = is->gcount();
        for (std::streamsize i = 0; i < g; ++i) {
            h ^= (uint8_t)buf[i];
            h *= 16777619u;
        }
        n += (uint32_t)g;
    }
    std::ostringstream os;
    os << "READBACK " << path << " len=" << n << " fnv=" << std::hex << h;
    logln(os.str());
}

void setup(void) {
    int rc = romfs_mount();
    {
        std::ostringstream os;
        os << "romfs_mount rc=" << rc << " count=" << romfs_count()
           << " rom_size=" << (unsigned)cron_rom_size();
        logln(os.str());
    }
    for (uint32_t i = 0; i < romfs_count() && i < 6; ++i) {
        std::string nm; uint32_t len;
        romfs_entry_name(i, nm, len);
        std::ostringstream os;
        os << "  entry[" << i << "] " << nm << " len=" << len;
        logln(os.str());
    }
    /* lowercase queries vs UPPERCASE DOS names = case-insensitive proof */
    readback("static/text.flx");
    readback("static/u7map");
    readback("static/palettes.flx");
    readback("static/does-not-exist.dat");
    cron_exit(0);
}

void frame(void) {}

CRONOPIO_CART_INIT(setup, frame)
