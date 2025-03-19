#ifndef SCAN_SCAN_H
#define SCAN_SCAN_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#include "macros.h"

#define SIST_SWS_ALGO SWS_LANCZOS

#define UNUSED(x) __attribute__((__unused__))  x

typedef void (*logf_callback_t)(const char *filepath, int level, char *format, ...);

typedef void (*log_callback_t)(const char *filepath, int level, char *str);

typedef int scan_code_t;
#define SCAN_OK (scan_code_t) 0
#define SCAN_ERR_READ (scan_code_t) (-1)
#define SCAN_ERR_SKIP (scan_code_t) (-2)

#define LEVEL_DEBUG 0
#define LEVEL_INFO 1
#define LEVEL_WARNING 2
#define LEVEL_ERROR 3
#define LEVEL_FATAL 4

#define CTX_LOG_DEBUGF(filepath, fmt, ...) ctx->logf(filepath, LEVEL_DEBUG, fmt, __VA_ARGS__)
#define CTX_LOG_DEBUG(filepath, str) ctx->log(filepath, LEVEL_DEBUG, str)

#define CTX_LOG_INFOF(filepath, fmt, ...) ctx->logf(filepath, LEVEL_INFO, fmt, __VA_ARGS__)
#define CTX_LOG_INFO(filepath, str) ctx->log(filepath, LEVEL_INFO, str)

#define CTX_LOG_WARNINGF(filepath, fmt, ...) ctx->logf(filepath, LEVEL_WARNING, fmt, __VA_ARGS__)
#define CTX_LOG_WARNING(filepath, str) ctx->log(filepath, LEVEL_WARNING, str)

#define CTX_LOG_ERRORF(filepath, fmt, ...) ctx->logf(filepath, LEVEL_ERROR, fmt, __VA_ARGS__)
#define CTX_LOG_ERROR(filepath, str) ctx->log(filepath, LEVEL_ERROR, str)

#define CTX_LOG_FATALF(filepath, fmt, ...) ctx->logf(filepath, LEVEL_FATAL, fmt, __VA_ARGS__); exit(-1)
#define CTX_LOG_FATAL(filepath, str) ctx->log(filepath, LEVEL_FATAL, str); exit(-1)

// 0000000.000000000
#define SIST_SID_LEN 18

#define EBOOK_LOCKS 0

enum metakey {
    // String
    MetaContent = 1,
    MetaMediaAudioCodec,
    MetaMediaVideoCodec,
    MetaArtist,
    MetaAlbum,
    MetaAlbumArtist,
    MetaGenre,
    MetaTitle,
    MetaMediaComment,
    MetaFontName,
    MetaExifMake,
    MetaExifDescription,
    MetaExifSoftware,
    MetaExifExposureTime,
    MetaExifFNumber,
    MetaExifFocalLength,
    MetaExifUserComment,
    MetaExifModel,
    MetaExifIsoSpeedRatings,
    MetaExifDateTime,
    MetaAuthor,
    MetaModifiedBy,
    MetaChecksum,

    // Number
    MetaWidth,
    MetaHeight,
    MetaMediaDuration,
    MetaMediaBitrate,
    MetaPages,

    // ??
    MetaExifGpsLongitudeDMS,
    MetaExifGpsLongitudeRef,
    MetaExifGpsLatitudeDMS,
    MetaExifGpsLatitudeRef,
    MetaExifGpsLatitudeDec,
    MetaExifGpsLongitudeDec,

    // other
    MetaThumbnail,
};

typedef struct meta_line {
    struct meta_line *next;
    enum metakey key;
    size_t size;
    union {
        char str_val[0];
        unsigned long long_val;
    };
} meta_line_t;


typedef struct document {
    unsigned long size;
    unsigned int mime;
    int mtime;
    int base;
    int ext;
    meta_line_t *meta_head;
    meta_line_t *meta_tail;
    int thumbnail_count;
    char filepath[PATH_MAX * 2 + 1];
    char parent[PATH_MAX * 2 + 1];
} document_t;

typedef struct vfile vfile_t;

__attribute__((warn_unused_result))
typedef int (*read_func_t)(struct vfile *, void *buf, size_t size);

__attribute__((warn_unused_result))
typedef long (*seek_func_t)(struct vfile *, long offset, int whence);

typedef void (*close_func_t)(struct vfile *);

typedef void (*reset_func_t)(struct vfile *);

typedef struct vfile {
    union {
        int fd;
        struct archive *arc;
        const void *_test_data;
    };

    int is_fs_file;
    int has_checksum;
    int calculate_checksum;
    char filepath[PATH_MAX * 2 + 1];

    int mtime;
    size_t st_size;

    EVP_MD_CTX *sha1_ctx;
    unsigned char sha1_digest[SHA1_DIGEST_LENGTH];

    void *rewind_buffer;
    int rewind_buffer_size;
    int rewind_buffer_cursor;

    read_func_t read;
    read_func_t read_rewindable;
    close_func_t close;
    reset_func_t reset;
    log_callback_t log;
    logf_callback_t logf;
} vfile_t;

typedef struct {
    int base;
    int ext;
    struct vfile vfile;
    char parent[PATH_MAX * 2 + 1];
    char filepath[PATH_MAX * 2 + 1];
} parse_job_t;

#define IS_SUB_JOB(job) ((job)->parent[0] != '\0')


#include "util.h"

typedef void (*parse_callback_t)(parse_job_t *job);

#endif
