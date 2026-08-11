#ifndef SCAN_TEST_UTIL_H
#define SCAN_TEST_UTIL_H

#include "libscan/scan.h"
#include <fcntl.h>
#include <unistd.h>

void load_file(const char *filepath, vfile_t *f);
void load_mem(void *mem, size_t size, vfile_t *f);
void load_doc_mem(void *mem, size_t mem_len, vfile_t *f, document_t *doc);
void load_doc_file(const char *filepath, vfile_t *f, document_t *doc);
void cleanup(document_t *doc, vfile_t *f);

static void noop_logf(const char *filepath, int level, char *format, ...) {
    // noop
}

static void noop_log(const char *filepath, int level, char *str) {
    // noop
}

static size_t store_size = 0;

static void counter_store(char* key, int num, void *value, size_t value_len) {
    store_size += value_len;
//    char id[37];
//    char tmp[PATH_MAX];
//    uuid_unparse(reinterpret_cast<const unsigned char *>(key), id);
//    sprintf(tmp, "%s.jpeg", id);
//    int fd = open(tmp, O_TRUNC|O_WRONLY|O_CREAT, 0777);
//    write(fd, value, value_len);
//    close(fd);
}

meta_line_t *get_meta(document_t *doc, metakey key);

meta_line_t *get_meta_from(meta_line_t *meta, metakey key);


#define CLOSE_FILE(f) if (f.close != NULL) {f.close(&f);};

void destroy_doc(document_t *doc);

void fuzz_buffer(char *buf, size_t *buf_len, int width, int n, int trunc_p);

#endif
