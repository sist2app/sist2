#ifndef SIST2_WORKER_SINK_H
#define SIST2_WORKER_SINK_H

#include "src/sist.h"
#include "libscan/scan.h"

/**
 * Where the output of a parser goes.
 *
 * In the process backend the sink writes frames to the master, which owns the only connection to the
 * index database; in the in-process thread backend it writes to that database directly.
 *
 * Thumbnails always belong to the document written just before them, so the sink has no need for
 * document ids.
 */
typedef struct {
    int (*mark_document)(const char *rel_path, int mtime);

    void (*write_document)(document_t *doc, const char *json);

    void (*write_thumbnail)(int index, const void *data, size_t size);

    /** Only called for documents nested inside an archive, for crash reporting */
    void (*set_current_job)(const char *filepath);
} document_sink_t;

extern __thread const document_sink_t *DocumentSink;

/**
 * @return TRUE when the document is already in the index with that mtime and must not be parsed.
 */
int sink_mark_document(const char *rel_path, int mtime);

void sink_write_document(document_t *doc, const char *json);

void sink_write_thumbnail(int index, const void *data, size_t size);

void sink_set_current_job(const char *filepath);

/** Writes straight to ProcData.index_db */
extern const document_sink_t DatabaseSink;

#endif
