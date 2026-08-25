#ifndef SIST_H
#define SIST_H

#ifndef    FALSE
#define    FALSE    (0)
#define BOOL int
#endif

#ifndef    TRUE
#define    TRUE    (!FALSE)
#endif

#undef    MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))

#undef    MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#undef ABS
#define ABS(a)       (((a) < 0) ? -(a) : (a))

#define UNUSED(x) __attribute__((__unused__))  x

#define MAX_THREADS (256)

#include "util.h"
#include "log.h"
#include "types.h"

#include "libscan/scan.h"

#include <cjson/cJSON.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <ctype.h>
#include "git_hash.h"

#define Q(x) #x
#define QUOTE(x) Q(x)

#define VERSION_MAJOR 4
#define VERSION_MINOR 2
#define VERSION_PATCH 0
#define VERSION QUOTE(VERSION_MAJOR) "." QUOTE(VERSION_MINOR) "." QUOTE(VERSION_PATCH)

static const char *const Version = VERSION;
static const int VersionMajor = VERSION_MAJOR;
static const int VersionMinor = VERSION_MINOR;
static const int VersionPatch = VERSION_PATCH;

#ifndef SIST_PLATFORM
#define SIST_PLATFORM unknown
#endif

#define EXPECTED_MONGOOSE_VERSION "7.22"

#endif
