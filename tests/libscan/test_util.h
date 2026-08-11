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

meta_line_t *get_meta(document_t *doc, metakey key);

meta_line_t *get_meta_from(meta_line_t *meta, metakey key);

/** Total byte size of all the thumbnails stored on the document */
size_t get_thumbnail_size(document_t *doc);

/** Number of MetaThumbnail meta lines stored on the document */
int get_thumbnail_meta_count(document_t *doc);


#define CLOSE_FILE(f) if (f.close != NULL) {f.close(&f);};

void destroy_doc(document_t *doc);

void fuzz_buffer(char *buf, size_t *buf_len, int width, int n, int trunc_p);

#endif
