/* Slice 1 PROPER probe: drive Exult's REAL file layer (files/utils.cc) through
 * the Cronopio bridge (files_cron). Proves (a) a real Black Gate STATIC file is
 * read back byte-exact via Exult's own U7open_in over the ROM, and (b) a
 * GAMEDAT write round-trips through the RAM-FS (write -> read back). The FNV for
 * static/text.flx must match exultpak's PACKED manifest. Throwaway harness for
 * the files/ slice (supersedes files_smoke); remove when the engine drives it. */
#include <cronopio.h> /* extern "C" guarded */

#include "files_cron.h"
#include "utils.h" /* Exult: U7open_in / U7open_out / U7exists */

#include <cstdint>
#include <exception>
#include <istream>
#include <ostream>
#include <sstream>
#include <string>

static void logln(const std::string& s) {
    std::string l = s;
    l.push_back('\n');
    cron_log(l.data(), (int32_t)l.size());
}

static uint32_t fnv_stream(std::istream& in, uint32_t& n) {
    uint32_t h = 2166136261u;
    char     buf[4096];
    n          = 0;
    while (in) {
        in.read(buf, sizeof buf);
        std::streamsize g = in.gcount();
        for (std::streamsize i = 0; i < g; ++i) {
            h ^= (uint8_t)buf[i];
            h *= 16777619u;
        }
        n += (uint32_t)g;
    }
    return h;
}

void setup(void) {
    int rc = exult_files_init();
    {
        std::ostringstream os;
        os << "exult_files_init rc=" << rc;
        logln(os.str());
    }

    /* (a) read a real STATIC file through Exult's own U7open_in (ROM path) */
    try {
        auto     in = U7open_in("<STATIC>/text.flx");
        uint32_t n;
        uint32_t h = fnv_stream(*in, n);
        std::ostringstream os;
        os << "U7open_in <STATIC>/text.flx len=" << n << " fnv=" << std::hex << h;
        logln(os.str());
    } catch (std::exception& e) {
        logln(std::string("EXC static read: ") + e.what());
    }

    /* U7exists over the ROM (no opendir needed: it opens as a file first) */
    logln(std::string("U7exists <STATIC>/u7map = ")
          + (U7exists("<STATIC>/u7map") ? "true" : "false"));

    /* (b) GAMEDAT write -> read-back round trip through the RAM-FS */
    try {
        {
            auto out = U7open_out("<GAMEDAT>/probe.dat");
            (*out) << "hello-cronopio";
        }
        auto        in = U7open_in("<GAMEDAT>/probe.dat");
        std::string got;
        std::getline(*in, got);
        logln(std::string("GAMEDAT round-trip got='") + got + "'");
    } catch (std::exception& e) {
        logln(std::string("EXC gamedat: ") + e.what());
    }

    cron_exit(0);
}

void frame(void) {}

CRONOPIO_CART_INIT(setup, frame)
