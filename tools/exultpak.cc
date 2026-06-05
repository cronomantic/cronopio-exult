/* exultpak — generic CronoFS bake tool (host-side, OUR OWN, no Exult tooling).
 *
 * Packs a directory tree byte-for-byte into a single CronoFS archive that the
 * cart bakes via `--rom=<archive>` and serves read-only through cron_rom().
 * Format-agnostic: it stores raw bytes for ANY file (FLEX/VGA/u7map/usecode/.dat
 * alike) — the Exult engine inside the cart parses formats, not this tool.
 *
 * Usage:  exultpak <out.rom> <src> <mount-prefix> [<src> <mount-prefix> ...]
 *   e.g.  exultpak exult.rom "E:/JuegosW/Ultima 7/STATIC" static \
 *                            "third_party/exult/data/exult.flx" data
 *   -> one or more (src, prefix) MOUNTS are merged into a single archive. A
 *      <src> may be a DIRECTORY (every file under it is keyed "<prefix>/<relpath>",
 *      recursive) OR a single FILE (keyed "<prefix>/<filename>"). Forward slashes,
 *      original case; lookup in the cart is case-insensitive. This lets the user's
 *      read-only <STATIC> game tree and Exult's own <DATA> extras (exult*.flx) live
 *      side by side in one ROM without copying the big STATIC tree to a staging dir.
 *
 * Prints a manifest line per file (path / length / FNV-1a) so a cart read-back
 * can be verified byte-exact. READS the source tree only; never writes into it.
 *
 * Build (host): clang++ -std=c++17 -O2 tools/exultpak.cc -o build/exultpak
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <algorithm>

#include "../src/romfs_fmt.h"

namespace fs = std::filesystem;

struct Item {
    std::string key;             // "<prefix>/<relpath>"
    std::vector<uint8_t> bytes;  // file contents
};

static void put_u32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back((uint8_t)(x));       v.push_back((uint8_t)(x >> 8));
    v.push_back((uint8_t)(x >> 16)); v.push_back((uint8_t)(x >> 24));
}

// Read one regular file into `items` under key. Returns false on read error.
static bool add_file(std::vector<Item> &items, const fs::path &p, std::string key) {
    std::ifstream f(p, std::ios::binary);
    if (!f) { std::fprintf(stderr, "exultpak: cannot read %s\n", key.c_str()); return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
    items.push_back(Item{ std::move(key), std::move(bytes) });
    return true;
}

int main(int argc, char **argv) {
    // argv[1] = out.rom, then (src, prefix) pairs.
    if (argc < 4 || ((argc - 2) % 2) != 0) {
        std::fprintf(stderr,
            "usage: exultpak <out.rom> <src> <mount-prefix> [<src> <mount-prefix> ...]\n");
        return 2;
    }
    fs::path out = argv[1];

    std::vector<Item> items;
    for (int a = 2; a + 1 < argc; a += 2) {
        fs::path    src    = argv[a];
        std::string prefix = argv[a + 1];
        // strip trailing slash on prefix
        while (!prefix.empty() && (prefix.back() == '/' || prefix.back() == '\\'))
            prefix.pop_back();

        if (fs::is_directory(src)) {
            for (auto &de : fs::recursive_directory_iterator(src)) {
                if (!de.is_regular_file()) continue;
                std::string rel = fs::relative(de.path(), src).generic_string(); // '/' sep
                std::string key = prefix.empty() ? rel : prefix + "/" + rel;
                if (!add_file(items, de.path(), std::move(key))) return 1;
            }
        } else if (fs::is_regular_file(src)) {
            std::string name = src.filename().generic_string();
            std::string key  = prefix.empty() ? name : prefix + "/" + name;
            if (!add_file(items, src, std::move(key))) return 1;
        } else {
            std::fprintf(stderr, "exultpak: '%s' is not a file or directory\n", argv[a]);
            return 1;
        }
    }
    std::sort(items.begin(), items.end(),
              [](const Item &a, const Item &b) { return a.key < b.key; });

    const uint32_t count     = (uint32_t)items.size();
    const uint32_t index_off = 32;                       // sizeof header
    const uint32_t names_off = index_off + count * 16u;  // sizeof entry
    // names blob
    std::vector<uint8_t> names;
    std::vector<uint32_t> name_at(count), name_len(count);
    for (uint32_t i = 0; i < count; ++i) {
        name_at[i]  = names_off + (uint32_t)names.size();
        name_len[i] = (uint32_t)items[i].key.size();
        names.insert(names.end(), items[i].key.begin(), items[i].key.end());
    }
    const uint32_t data_off = names_off + (uint32_t)names.size();
    std::vector<uint32_t> data_at(count);
    uint32_t cursor = data_off;
    for (uint32_t i = 0; i < count; ++i) {
        data_at[i] = cursor;
        cursor += (uint32_t)items[i].bytes.size();
    }
    const uint32_t total = cursor;

    std::vector<uint8_t> blob;
    blob.reserve(total);
    // header
    blob.insert(blob.end(), ROMFS_MAGIC, ROMFS_MAGIC + 8);
    put_u32(blob, ROMFS_VERSION);
    put_u32(blob, count);
    put_u32(blob, index_off);
    put_u32(blob, names_off);
    put_u32(blob, data_off);
    put_u32(blob, total);
    // index
    for (uint32_t i = 0; i < count; ++i) {
        put_u32(blob, name_at[i]);
        put_u32(blob, name_len[i]);
        put_u32(blob, data_at[i]);
        put_u32(blob, (uint32_t)items[i].bytes.size());
    }
    // names + data
    blob.insert(blob.end(), names.begin(), names.end());
    for (auto &it : items)
        blob.insert(blob.end(), it.bytes.begin(), it.bytes.end());

    std::ofstream of(out, std::ios::binary);
    if (!of) { std::fprintf(stderr, "exultpak: cannot write %s\n", argv[1]); return 1; }
    of.write((const char *)blob.data(), (std::streamsize)blob.size());
    of.close();

    // manifest (stderr so a build can capture it separately from any stdout use)
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t fnv = romfs_fnv1a(items[i].bytes.data(), (uint32_t)items[i].bytes.size());
        std::fprintf(stderr, "PACKED %-28s len=%-9u fnv=%08x\n",
                     items[i].key.c_str(), (uint32_t)items[i].bytes.size(), fnv);
    }
    std::fprintf(stderr, "exultpak: %u files, %u bytes -> %s\n", count, total, argv[1]);
    return 0;
}
