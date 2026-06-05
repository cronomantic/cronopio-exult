/* POSIX backend for the vendored libc++ std::filesystem (cxxio.bc).
 *
 * Exult's gamedat crash-backup (gamedat.cc:1229-1269) is the ONLY user of
 * std::filesystem, and it is NEVER on the normal init/paint path. The vendored
 * libc++ filesystem TUs (operations.cpp / directory_iterator.cpp / ...) call a
 * POSIX surface that picolibc declares but does not implement and cron_sys.c
 * does not provide. We supply the missing entry points here as inert ENOSYS
 * stubs so the engine TRANSLATES + LINKS; at runtime any actual filesystem op
 * fails with an error_code / throws filesystem_error (fine — crash-backup is
 * best-effort and off the live path). lstat routes to the real cron_sys.c stat.
 *
 * Compiled cart-side by cvm-cc against the SAME picolibc headers the filesystem
 * TUs use, so the signatures/ABI match the callers exactly (cron_sys.c compiles
 * against the SDK header world, which is why these don't live there). If/when a
 * second C++ cart wants real directory ops, promote this to a shared runtime
 * file backed by the cron RAM-FS. See memory exult-gamewin-phase1. */
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

/* No symlinks in the cron RAM-FS — lstat is just stat (cron_sys.c). */
int lstat(const char *path, struct stat *buf) { return stat(path, buf); }

int   openat(int dirfd, const char *path, int flags, ...) {
    (void)dirfd; (void)path; (void)flags; errno = ENOSYS; return -1;
}
DIR  *fdopendir(int fd)                         { (void)fd; errno = ENOSYS; return 0; }
int   fchmod(int fd, mode_t m)                  { (void)fd; (void)m; errno = ENOSYS; return -1; }
int   fchmodat(int fd, const char *p, mode_t m, int f) {
    (void)fd; (void)p; (void)m; (void)f; errno = ENOSYS; return -1;
}
int   ftruncate(int fd, off_t len)              { (void)fd; (void)len; errno = ENOSYS; return -1; }
int   truncate(const char *p, off_t len)        { (void)p; (void)len; errno = ENOSYS; return -1; }
int   link(const char *a, const char *b)        { (void)a; (void)b; errno = ENOSYS; return -1; }
int   symlink(const char *a, const char *b)     { (void)a; (void)b; errno = ENOSYS; return -1; }
ssize_t readlink(const char *p, char *b, size_t n) {
    (void)p; (void)b; (void)n; errno = ENOSYS; return -1;
}
char *realpath(const char *p, char *out)        { (void)p; (void)out; errno = ENOSYS; return 0; }
int   statvfs(const char *p, struct statvfs *b) { (void)p; (void)b; errno = ENOSYS; return -1; }
long  pathconf(const char *p, int name)         { (void)p; (void)name; errno = ENOSYS; return -1; }
int   unlinkat(int fd, const char *p, int f)    { (void)fd; (void)p; (void)f; errno = ENOSYS; return -1; }
int   utimes(const char *p, const struct timeval tv[2]) {
    (void)p; (void)tv; errno = ENOSYS; return -1;
}
/* No wall-clock device wired for filesystem timestamps; fail cleanly (timing on
 * the live path uses SDL_GetTicks -> cron_time_ms, not gettimeofday). */
int   gettimeofday(struct timeval *tv, void *tz) {
    (void)tv; (void)tz; errno = ENOSYS; return -1;
}
