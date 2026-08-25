#ifndef SCAN_MACROS_H
#define SCAN_MACROS_H

#include "win32.h"

#include <string.h>

// glibc defines __always_inline in sys/cdefs.h; musl does not
#ifndef __always_inline
#define __always_inline inline __attribute__((__always_inline__))
#endif

// Windows opens files in text mode by default, which would rewrite CRLF and stop at 0x1a
#ifndef O_BINARY
#define O_BINARY 0
#endif

// __MINGW_PRINTF_FORMAT selects the archetype matching the printf actually linked, so that %zu
// and %lld are checked rather than rejected
#ifdef _WIN32
#define SIST_PRINTF_FORMAT __MINGW_PRINTF_FORMAT
#else
#define SIST_PRINTF_FORMAT printf
#endif

// long is 32 bits on Windows, so the plain fseek()/ftell() cannot reach past 2GB there
#ifdef _WIN32
#define sist_fseek _fseeki64
#define sist_ftell _ftelli64
#else
#define sist_fseek fseeko
#define sist_ftell ftello
#endif

// Paths are UTF-8. On Windows that needs the wide CRT calls underneath, which win32.h declares.
#ifndef _WIN32
#define sist_open open
#define sist_fopen fopen
#endif

// mingw's struct stat carries the single-second st_mtime rather than POSIX 2008's st_mtim
#ifdef _WIN32
#define STAT_MTIME(st) ((st).st_mtime)
// libuv reports no termination signal on Windows, so this names a case that cannot arise there
#define sist_signal_name(sig) "terminated"
#else
#define STAT_MTIME(st) ((st).st_mtim.tv_sec)
#define sist_lstat lstat
#define sist_stat stat
#define sist_signal_name(sig) strsignal(sig)

// The temporary-file fallback when no environment variable nominates one
#define sist_temp_dir() "/tmp"

// The Windows implementations live in win32.c: mongoose exports opendir(), readdir() and
// closedir() with an incompatible struct dirent there, and a static link binds those names to
// whichever definition it reaches first.
#include <dirent.h>
#include <sys/stat.h>

typedef DIR sist_dir_t;
#define sist_opendir opendir
#define sist_closedir closedir

// The Windows implementation fills *info from the directory stream; here the caller stats
static inline const char *sist_readdir(sist_dir_t *dir, struct stat *info) {
    (void) info;
    const struct dirent *entry = readdir(dir);
    return entry == NULL ? NULL : entry->d_name;
}
#endif

#ifndef FALSE
#define FALSE (0)
#define BOOL int
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

#undef MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

#undef MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Strip trailing slashes. A drive root keeps its slash: "C:" names the working directory on C:,
// not the root of it.
static inline void path_strip_trailing_slashes(char *path) {
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/' && path[len - 2] != ':') {
        path[--len] = '\0';
    }
}

#undef ABS
#define ABS(a) (((a) < 0) ? -(a) : (a))

#define SHA1_DIGEST_LENGTH SHA_DIGEST_LENGTH

#define SHA1_STR_LENGTH (SHA1_DIGEST_LENGTH * 2 + 1)
#define MD5_STR_LENGTH (MD5_DIGEST_LENGTH * 2 + 1)

#define APPEND_STR_META(doc, keyname, value) do {\
    {meta_line_t *meta_str = malloc(sizeof(meta_line_t) + strlen(value)); \
    meta_str->key = keyname; \
    strcpy(meta_str->str_val, value); \
    APPEND_META(doc, meta_str);}} while(0)

#define APPEND_LONG_META(doc, keyname, value) do{\
    {meta_line_t *meta_long = malloc(sizeof(meta_line_t)); \
    meta_long->key = keyname; \
    meta_long->long_val = value; \
    APPEND_META(doc, meta_long);}} while(0)

#define APPEND_THUMBNAIL(doc, data, data_len) do{ \
    {meta_line_t *meta_tn = malloc(sizeof(meta_line_t) + (data_len)); \
    meta_tn->key = MetaThumbnail; \
    meta_tn->size = data_len; \
    memcpy(meta_tn->str_val, data, data_len); \
    APPEND_META(doc, meta_tn);}} while(0)


#define APPEND_META(doc, meta) do {\
    (meta)->next = NULL;\
    if ((doc)->meta_head == NULL) {\
        (doc)->meta_head = meta;\
        (doc)->meta_tail = (doc)->meta_head;\
    } else {\
        (doc)->meta_tail->next = meta;\
        (doc)->meta_tail = meta;\
    }}while(0)

#define APPEND_UTF8_META(doc, keyname, str) \
    text_buffer_t tex = text_buffer_create(-1); \
    text_buffer_append_string0(&tex, str); \
    text_buffer_terminate_string(&tex); \
    meta_line_t *meta_tag = malloc(sizeof(meta_line_t) + tex.dyn_buffer.cur); \
    meta_tag->key = keyname; \
    strcpy(meta_tag->str_val, tex.dyn_buffer.buf); \
    APPEND_META(doc, meta_tag); \
    text_buffer_destroy(&tex)

#endif
