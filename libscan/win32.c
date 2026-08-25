#ifdef _WIN32

// Before stdlib.h, or rand_s is not declared
#define _CRT_RAND_S

#include "win32.h"
#include "macros.h"

#include <errno.h>
#include <io.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#define NOGDI
#include <windows.h>

// The rest of sist2 builds, compares and stores paths with forward slashes: the Win32 calls take
// them, and the index stays readable by a build on any platform.
static void to_forward_slashes(char *path) {
    for (char *c = path; *c != '\0'; c++) {
        if (*c == '\\') {
            *c = '/';
        }
    }
}

// Paths are UTF-8 everywhere in sist2. The wide Win32 calls are used underneath so that a
// filename outside the process code page still round-trips, whatever that code page is.
static wchar_t *utf8_to_wide(const char *utf8) {
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0) {
        return NULL;
    }

    wchar_t *wide = malloc((size_t) len * sizeof(wchar_t));
    if (wide == NULL) {
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, len) <= 0) {
        free(wide);
        return NULL;
    }

    return wide;
}

static int wide_to_utf8(const wchar_t *wide, char *out, int out_size) {
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, out_size, NULL, NULL);
}

static int win32_error_to_errno(DWORD error) {
    switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_NAME:
        case ERROR_BAD_NETPATH:
            return ENOENT;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            return EACCES;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            return ENOMEM;
        case ERROR_FILENAME_EXCED_RANGE:
            return ENAMETOOLONG;
        case ERROR_DIRECTORY:
            return ENOTDIR;
        default:
            return EIO;
    }
}

// The plain calls fail past 260 characters unless the user opted into long paths in the registry;
// the \\?\ form always takes them, but requires backslashes and an absolute path. Frees `wide`.
static wchar_t *wide_path_to_extended(wchar_t *wide) {
    if (wcslen(wide) < MAX_PATH || wcsncmp(wide, L"\\\\?\\", 4) == 0) {
        return wide;
    }

    for (wchar_t *c = wide; *c != L'\0'; c++) {
        if (*c == L'/') {
            *c = L'\\';
        }
    }

    wchar_t *absolute = wide;
    if (wide[1] != L':' && !(wide[0] == L'\\' && wide[1] == L'\\')) {
        absolute = _wfullpath(NULL, wide, 0);
        if (absolute == NULL) {
            absolute = wide;
        }
    }

    const int unc = absolute[0] == L'\\' && absolute[1] == L'\\';
    const wchar_t *prefix = unc ? L"\\\\?\\UNC\\" : L"\\\\?\\";
    const wchar_t *rest = unc ? absolute + 2 : absolute;

    wchar_t *extended = malloc((wcslen(prefix) + wcslen(rest) + 1) * sizeof(wchar_t));
    if (extended != NULL) {
        wcscpy(extended, prefix);
        wcscat(extended, rest);
    }

    if (absolute != wide) {
        free(absolute);
    }
    free(wide);
    return extended;
}

static wchar_t *utf8_to_wide_path(const char *utf8) {
    wchar_t *wide = utf8_to_wide(utf8);
    return wide == NULL ? NULL : wide_path_to_extended(wide);
}

int sist_open(const char *path, int flags) {
    wchar_t *wide = utf8_to_wide_path(path);
    if (wide == NULL) {
        errno = EINVAL;
        return -1;
    }

    const int fd = _wopen(wide, flags);
    free(wide);
    return fd;
}

FILE *sist_fopen(const char *path, const char *mode) {
    wchar_t *wide_path = utf8_to_wide_path(path);
    wchar_t *wide_mode = utf8_to_wide(mode);
    FILE *file = NULL;

    if (wide_path != NULL && wide_mode != NULL) {
        file = _wfopen(wide_path, wide_mode);
    } else {
        errno = EINVAL;
    }

    free(wide_path);
    free(wide_mode);
    return file;
}

char *realpath(const char *path, char *resolved) {
    wchar_t *wide = utf8_to_wide(path);
    if (wide == NULL) {
        errno = EINVAL;
        return NULL;
    }

    wchar_t *wide_full = _wfullpath(NULL, wide, 0);
    free(wide);

    if (wide_full == NULL) {
        return NULL;
    }

    // realpath() resolves an existing path only
    if (_waccess(wide_full, 0) != 0) {
        free(wide_full);
        errno = ENOENT;
        return NULL;
    }

    char *full = malloc(PATH_MAX);
    if (full == NULL || wide_to_utf8(wide_full, full, PATH_MAX) <= 0) {
        free(wide_full);
        free(full);
        errno = ENAMETOOLONG;
        return NULL;
    }
    free(wide_full);

    to_forward_slashes(full);

    if (resolved == NULL) {
        return full;
    }

    strcpy(resolved, full);
    free(full);
    return resolved;
}

char *sist_strndup(const char *str, size_t size) {
    size_t len = strnlen(str, size);
    char *copy = malloc(len + 1);

    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, str, len);
    copy[len] = '\0';
    return copy;
}

static int random_state;

char *initstate(unsigned int seed, char *state, size_t size) {
    (void) size;

    random_state = (int) (seed == 0 ? 1 : seed);
    return state;
}

long random(void) {
    random_state = (int) (((unsigned int) random_state * 1103515245U + 12345U) & 0x7fffffffU);
    return random_state;
}

struct tm *localtime_r(const time_t *timer, struct tm *result) {
    return localtime_s(result, timer) == 0 ? result : NULL;
}

FILE *sist_tmpfile(void) {
    char dir[MAX_PATH + 1];
    char name[MAX_PATH + 1];

    // tmpfile() puts its file in the root of the current drive, which is not writable for a
    // regular user on a default install
    if (GetTempPathA(sizeof(dir), dir) == 0) {
        return NULL;
    }

    if (GetTempFileNameA(dir, "sist2", 0, name) == 0) {
        return NULL;
    }

    // "D" asks the CRT to delete the file when the last handle to it is closed
    return fopen(name, "w+bD");
}

FILE *fmemopen(void *buf, size_t size, const char *mode) {
    (void) mode;

    FILE *file = sist_tmpfile();

    if (file == NULL) {
        return NULL;
    }

    if (size != 0 && fwrite(buf, 1, size, file) != size) {
        fclose(file);
        return NULL;
    }

    rewind(file);
    return file;
}

// FILETIME counts 100ns intervals from 1601-01-01; the Unix epoch is 11644473600 seconds later
static time_t filetime_to_unix(const FILETIME filetime) {
    ULARGE_INTEGER ticks;
    ticks.LowPart = filetime.dwLowDateTime;
    ticks.HighPart = filetime.dwHighDateTime;

    return (time_t) (ticks.QuadPart / 10000000ULL - 11644473600ULL);
}

// Only an actual link is reported as one: cloud placeholders (OneDrive, Dropbox), dedup files and
// app-exec links all carry FILE_ATTRIBUTE_REPARSE_POINT too, and must scan as the file or
// directory they are.
static int reparse_is_link(DWORD attributes, DWORD tag) {
    if (!(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return 0;
    }

    return tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT;
}

static void attributes_to_stat(DWORD attributes, DWORD reparse_tag, int64_t size,
                               FILETIME mtime, struct stat *info) {
    memset(info, 0, sizeof(*info));

    if (reparse_is_link(attributes, reparse_tag)) {
        // Not followed, exactly as lstat() does not follow a symlink
        info->st_mode = S_IFLNK;
    } else if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        info->st_mode = S_IFDIR | 0755;
    } else {
        info->st_mode = S_IFREG | 0644;
    }

    info->st_size = size;
    info->st_mtime = filetime_to_unix(mtime);
}

// Built from the Win32 attributes rather than the CRT's stat(): the A-suffixed calls follow the
// process code page, which the manifest sets to UTF-8, while the CRT converts through its own.
int sist_lstat(const char *path, struct stat *info) {
    WIN32_FILE_ATTRIBUTE_DATA data;

    wchar_t *wide = utf8_to_wide_path(path);
    if (wide == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (!GetFileAttributesExW(wide, GetFileExInfoStandard, &data)) {
        errno = win32_error_to_errno(GetLastError());
        free(wide);
        return -1;
    }

    // The reparse tag says whether the point is a link; only a find handle reports it
    DWORD tag = 0;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        WIN32_FIND_DATAW find_data;
        const HANDLE handle = FindFirstFileW(wide, &find_data);
        if (handle != INVALID_HANDLE_VALUE) {
            tag = find_data.dwReserved0;
            FindClose(handle);
        }
    }
    free(wide);

    attributes_to_stat(data.dwFileAttributes, tag,
                       (((int64_t) data.nFileSizeHigh) << 32) | data.nFileSizeLow,
                       data.ftLastWriteTime, info);

    return 0;
}

// stat() taking a UTF-8 path, following reparse points the way it follows a symlink elsewhere.
// The CRT's narrow call converts through its own code page rather than the manifest's UTF-8.
int sist_stat(const char *path, struct stat *info) {
    struct _stat64 data;

    wchar_t *wide = utf8_to_wide_path(path);
    if (wide == NULL) {
        errno = EINVAL;
        return -1;
    }

    const int ret = _wstat64(wide, &data);
    free(wide);

    if (ret != 0) {
        return -1;
    }

    memset(info, 0, sizeof(*info));

    info->st_mode = data.st_mode;
    info->st_size = data.st_size;
    info->st_mtime = data.st_mtime;

    return 0;
}

double sist_free_space_mib(const char *directory) {
    ULARGE_INTEGER available;

    wchar_t *wide = utf8_to_wide(directory);
    if (wide == NULL) {
        return -1;
    }

    const BOOL ok = GetDiskFreeSpaceExW(wide, &available, NULL, NULL);
    free(wide);

    if (!ok) {
        return -1;
    }

    return (double) available.QuadPart / (1024 * 1024);
}

int sist_random_bytes(void *buf, size_t size) {
    unsigned char *out = buf;

    while (size > 0) {
        unsigned int value;
        if (rand_s(&value) != 0) {
            return -1;
        }

        const size_t chunk = size < sizeof(value) ? size : sizeof(value);
        memcpy(out, &value, chunk);
        out += chunk;
        size -= chunk;
    }

    return 0;
}

const char *sist_temp_dir(void) {
    static char directory[MAX_PATH + 1];

    if (directory[0] == '\0') {
        if (GetTempPathA(sizeof(directory), directory) == 0) {
            strcpy(directory, ".");
        } else {
            to_forward_slashes(directory);
            path_strip_trailing_slashes(directory);
        }
    }

    return directory;
}

char *sist_expand_env(const char *path) {
    char home[PATH_MAX];
    const char *source = path;

    if (path[0] == '~' && (path[1] == '\0' || path[1] == '/' || path[1] == '\\')) {
        // A value too long for the buffer comes back as the size it needs, with nothing written
        DWORD len = GetEnvironmentVariableA("HOME", home, sizeof(home));
        if (len == 0 || len >= sizeof(home)) {
            len = GetEnvironmentVariableA("USERPROFILE", home, sizeof(home));
        }
        if (len == 0 || len >= sizeof(home)) {
            return NULL;
        }
        if (len + strlen(path) >= sizeof(home)) {
            return NULL;
        }
        strcat(home, path + 1);
        source = home;
    }

    char *expanded = malloc(PATH_MAX);
    if (expanded == NULL) {
        return NULL;
    }

    const DWORD len = ExpandEnvironmentStringsA(source, expanded, PATH_MAX);
    if (len == 0 || len > PATH_MAX) {
        free(expanded);
        return NULL;
    }

    to_forward_slashes(expanded);
    return expanded;
}

struct sist_dir {
    HANDLE handle;
    WIN32_FIND_DATAW entry;
    char name[PATH_MAX];
    int exhausted;
    int pending;
};

sist_dir_t *sist_opendir(const char *path) {
    char pattern[PATH_MAX];
    const size_t len = strlen(path);
    const char *separator = (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\')) ? "" : "/";

    if (snprintf(pattern, sizeof(pattern), "%s%s*", path, separator) >= (int) sizeof(pattern)) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    wchar_t *wide_pattern = utf8_to_wide_path(pattern);
    if (wide_pattern == NULL) {
        errno = EINVAL;
        return NULL;
    }

    sist_dir_t *dir = calloc(1, sizeof(*dir));
    if (dir == NULL) {
        free(wide_pattern);
        return NULL;
    }

    dir->handle = FindFirstFileW(wide_pattern, &dir->entry);
    free(wide_pattern);

    if (dir->handle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        // An empty directory is not a failure, but there is nothing to iterate either
        if (error == ERROR_FILE_NOT_FOUND) {
            dir->exhausted = 1;
            return dir;
        }
        free(dir);
        errno = win32_error_to_errno(error);
        return NULL;
    }

    dir->pending = 1;
    return dir;
}

const char *sist_readdir(sist_dir_t *dir, struct stat *info) {
    while (!dir->exhausted) {
        if (dir->pending) {
            dir->pending = 0;
        } else if (!FindNextFileW(dir->handle, &dir->entry)) {
            dir->exhausted = 1;
            break;
        }

        // A name that does not fit is skipped rather than ending the walk
        if (wide_to_utf8(dir->entry.cFileName, dir->name, sizeof(dir->name)) > 0) {
            attributes_to_stat(dir->entry.dwFileAttributes, dir->entry.dwReserved0,
                               (((int64_t) dir->entry.nFileSizeHigh) << 32) | dir->entry.nFileSizeLow,
                               dir->entry.ftLastWriteTime, info);
            return dir->name;
        }
    }

    return NULL;
}

void sist_closedir(sist_dir_t *dir) {
    if (dir == NULL) {
        return;
    }
    if (dir->handle != INVALID_HANDLE_VALUE) {
        FindClose(dir->handle);
    }
    free(dir);
}

void sist_enable_console_vt(void) {
    const HANDLE handle = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode;

    if (handle == INVALID_HANDLE_VALUE || !GetConsoleMode(handle, &mode)) {
        return;
    }

    SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

#endif

// -Wpedantic rejects an empty translation unit, which this is everywhere but Windows
typedef int sist_win32_not_empty_t;
