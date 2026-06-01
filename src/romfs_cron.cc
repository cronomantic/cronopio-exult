#include "romfs_cron.h"
#include "romfs_fmt.h"

#include <cronopio.h>   /* cron_rom / cron_rom_size (extern "C" guarded) */

#include <streambuf>

/* ---- mounted state (pointers into ROM; ROM stays resident for the run) ---- */
static const uint8_t        *g_rom   = nullptr;
static const romfs_header_t *g_hdr   = nullptr;
static const romfs_entry_t  *g_index = nullptr;
static int                   g_mounted = 0;

static inline uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int romfs_mount(void) {
    if (g_mounted) return 0;
    const uint8_t *rom = cron_rom();
    uint32_t sz = cron_rom_size();
    if (!rom || sz < sizeof(romfs_header_t)) return -1;
    for (int i = 0; i < 8; ++i)
        if (rom[i] != (uint8_t)ROMFS_MAGIC[i]) return -2;
    const romfs_header_t *h = (const romfs_header_t *)rom;
    if (rd_u32((const uint8_t *)&h->version) != ROMFS_VERSION) return -2;
    g_rom     = rom;
    g_hdr     = h;
    g_index   = (const romfs_entry_t *)(rom + rd_u32((const uint8_t *)&h->index_off));
    g_mounted = 1;
    return 0;
}

uint32_t romfs_count(void) {
    return g_mounted ? rd_u32((const uint8_t *)&g_hdr->count) : 0;
}

bool romfs_entry_name(uint32_t i, std::string &out_path, uint32_t &out_len) {
    if (!g_mounted || i >= romfs_count()) return false;
    const romfs_entry_t *e = &g_index[i];
    uint32_t noff = rd_u32((const uint8_t *)&e->name_off);
    uint32_t nlen = rd_u32((const uint8_t *)&e->name_len);
    out_path.assign((const char *)(g_rom + noff), nlen);
    out_len = rd_u32((const uint8_t *)&e->data_len);
    return true;
}

/* case-insensitive, '\\'==' /', length-bounded compare of a query string against
 * a stored (offset,len) name. */
static bool name_eq(const char *q, const uint8_t *name, uint32_t nlen) {
    uint32_t i = 0;
    for (; i < nlen && q[i]; ++i) {
        char a = q[i], b = (char)name[i];
        if (a == '\\') a = '/';
        if (b == '\\') b = '/';
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return i == nlen && q[i] == '\0';
}

static const romfs_entry_t *romfs_find(const char *path) {
    if (!g_mounted || !path) return nullptr;
    uint32_t n = romfs_count();
    for (uint32_t i = 0; i < n; ++i) {
        const romfs_entry_t *e = &g_index[i];
        uint32_t noff = rd_u32((const uint8_t *)&e->name_off);
        uint32_t nlen = rd_u32((const uint8_t *)&e->name_len);
        if (name_eq(path, g_rom + noff, nlen)) return e;
    }
    return nullptr;
}

bool romfs_exists(const char *path) { return romfs_find(path) != nullptr; }

/* ---- zero-copy read-only streambuf over a ROM span ---- */
namespace {
class RomStreamBuf : public std::streambuf {
public:
    RomStreamBuf(const char *base, size_t len) : base_(base), len_(len) {
        char *p = const_cast<char *>(base_);
        setg(p, p, p + len_);   /* whole span is the get area */
    }
protected:
    pos_type seekoff(off_type off, std::ios_base::seekdir dir,
                     std::ios_base::openmode which) override {
        if (!(which & std::ios_base::in)) return pos_type(off_type(-1));
        char *p = const_cast<char *>(base_);
        off_type cur = gptr() - eback();
        off_type tgt = (dir == std::ios_base::beg) ? off
                     : (dir == std::ios_base::cur) ? cur + off
                     :                               (off_type)len_ + off;
        if (tgt < 0 || tgt > (off_type)len_) return pos_type(off_type(-1));
        setg(p, p + tgt, p + len_);
        return pos_type(tgt);
    }
    pos_type seekpos(pos_type pos, std::ios_base::openmode which) override {
        return seekoff((off_type)pos, std::ios_base::beg, which);
    }
private:
    const char *base_;
    size_t      len_;
};

class RomIStream : public std::istream {
public:
    RomIStream(const char *base, size_t len)
        : std::istream(nullptr), buf_(base, len) { rdbuf(&buf_); }
private:
    RomStreamBuf buf_;
};
} // namespace

std::unique_ptr<std::istream> romfs_open(const char *path) {
    const romfs_entry_t *e = romfs_find(path);
    if (!e) return nullptr;
    uint32_t doff = rd_u32((const uint8_t *)&e->data_off);
    uint32_t dlen = rd_u32((const uint8_t *)&e->data_len);
    return std::unique_ptr<std::istream>(
        new RomIStream((const char *)(g_rom + doff), dlen));
}
