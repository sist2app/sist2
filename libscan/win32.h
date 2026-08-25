#ifndef SCAN_WIN32_H
#define SCAN_WIN32_H

#ifdef _WIN32

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

// mingw sets PATH_MAX to MAX_PATH. sist2's own path buffers are not bound by it: the Win32 calls
// underneath take longer paths, and index roots are absolute.
#undef PATH_MAX
#define PATH_MAX 4096

// Not in mingw's sys/stat.h. The value is free in the CRT's _S_IFMT space.
#ifndef S_IFLNK
#define S_IFLNK 0xA000
#endif
#ifndef S_ISLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif

char *realpath(const char *path, char *resolved);

/**
 * open() and fopen() taking a UTF-8 path. The CRT's narrow calls convert through the process code
 * page, which only carries every filename when the UTF-8 manifest applies; these always do.
 */
int sist_open(const char *path, int flags);

FILE *sist_fopen(const char *path, const char *mode);

// Prefixed and remapped: libmagic's Windows compat layer exports a strndup of its own, and two
// definitions of it in one link is an error
char *sist_strndup(const char *str, size_t size);
#define strndup sist_strndup

struct tm *localtime_r(const time_t *timer, struct tm *result);

/**
 * glibc's TYPE_0 generator, which is what initstate() selects for a state buffer this small.
 * Reproduced rather than substituted so that a given seed yields the sequence it does elsewhere.
 */
char *initstate(unsigned int seed, char *state, size_t size);

long random(void);

FILE *fmemopen(void *buf, size_t size, const char *mode);

/** A binary temporary file that the CRT removes when the last handle to it closes */
FILE *sist_tmpfile(void);

/**
 * stat() that reports a reparse point as S_IFLNK instead of following it, so a walk sees the
 * same thing lstat() shows it elsewhere.
 */
int sist_lstat(const char *path, struct stat *info);

/** stat() taking a UTF-8 path, for the same reason sist_open() exists */
int sist_stat(const char *path, struct stat *info);

/** Free space on the volume holding `directory`, in MiB, or -1 when it cannot be determined */
double sist_free_space_mib(const char *directory);

/** Fills `buf` with cryptographically random bytes. Returns 0 on success. */
int sist_random_bytes(void *buf, size_t size);

/** The folder Windows nominates for temporary files. Never NULL. */
const char *sist_temp_dir(void);

/** Expands %VAR% references and a leading ~. Caller frees. NULL when `path` does not fit. */
char *sist_expand_env(const char *path);

/** Turns on ANSI escape handling for the console attached to stderr, when there is one. */
void sist_enable_console_vt(void);

/**
 * Directory iteration over the Win32 calls. mongoose exports opendir(), readdir() and closedir()
 * of its own with an incompatible struct dirent, and a static link binds those names to whichever
 * definition it reaches first, so the CRT's are not safe to call from here.
 */
typedef struct sist_dir sist_dir_t;

sist_dir_t *sist_opendir(const char *path);

/**
 * The next entry's name, or NULL at the end of the directory. Valid until the next call.
 * Fills *info from the find data the directory stream already carries, so the caller does not
 * pay a second metadata round-trip per entry.
 */
const char *sist_readdir(sist_dir_t *dir, struct stat *info);

void sist_closedir(sist_dir_t *dir);

#endif
#endif
