#ifndef SIST2_TYPES_H
#define SIST2_TYPES_H

#include <stdint.h>

typedef struct database database_t;

typedef struct index_descriptor {
    int id;
    char version[64];
    int version_major;
    int version_minor;
    int version_patch;
    int64_t timestamp;
    char root[PATH_MAX];
    char rewrite_url[8192];
    int root_len;
    char name[1024];
} index_descriptor_t;

typedef struct index_t {
    struct index_descriptor desc;

    database_t *db;

    char path[PATH_MAX];
} index_t;

typedef struct {
    int doc_id;
    int index_id;
    int64_t sid_int64;
    char sid_str[SIST_SID_LEN];
} sist_id_t;

#endif
