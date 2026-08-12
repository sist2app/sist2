#include "sink.h"

#include "src/ctx.h"

__thread const document_sink_t *DocumentSink = NULL;

int sink_mark_document(const char *rel_path, const int mtime) {
    return DocumentSink->mark_document(rel_path, mtime);
}

void sink_write_document(document_t *doc, const char *json) {
    DocumentSink->write_document(doc, json);
}

void sink_write_thumbnail(const int index, const void *data, const size_t size) {
    DocumentSink->write_thumbnail(index, data, size);
}

void sink_set_current_job(const char *filepath) {
    DocumentSink->set_current_job(filepath);
}

static int database_sink_mark_document(const char *rel_path, const int mtime) {
    return database_mark_document(ProcData.index_db, rel_path, mtime);
}

static void database_sink_write_document(document_t *doc, const char *json) {
    ProcData.last_doc_id = database_write_document(ProcData.index_db, doc, json);
}

static void database_sink_write_thumbnail(const int index, const void *data, const size_t size) {
    database_write_thumbnail(ProcData.index_db, ProcData.last_doc_id, index, (void *) data, size);
}

static void database_sink_set_current_job(UNUSED(const char *filepath)) {
    // Nothing to report: a crash takes the whole process down with the index database
}

const document_sink_t DatabaseSink = {
        .mark_document = database_sink_mark_document,
        .write_document = database_sink_write_document,
        .write_thumbnail = database_sink_write_thumbnail,
        .set_current_job = database_sink_set_current_job,
};
