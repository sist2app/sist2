#include "worker.h"

#include "protocol.h"
#include "sink.h"
#include "src/ctx.h"
#include "src/parsing/parse.h"

static void send_frame(uint32_t type, char *payload, uint32_t len) {
    int ret = frame_write(WORKER_OUT_FD, type, payload, len);
    free(payload);

    if (ret != 0) {
        // The master is gone; there is nothing left to do and nobody to report it to
        exit(0);
    }
}

static int worker_mark_document(const char *rel_path, int mtime) {
    // Both buffers are SIST_PATH_MAX, and rel_path is an offset into an even shorter one
    proto_mark_t mark = {.mtime = mtime};
    strcpy(mark.path, rel_path);

    uint32_t len;
    char *payload = proto_encode_mark(&mark, &len);
    send_frame(FRAME_REQ_MARK, payload, len);

    frame_t frame;
    if (frame_read(WORKER_IN_FD, &frame) != 0) {
        exit(0);
    }

    if (frame.type != FRAME_RSP_MARK) {
        LOG_FATALF("worker.c", "FIXME: expected RSP_MARK, got frame type %d", frame.type);
    }

    int32_t exists = FALSE;
    if (proto_decode_i32(&frame, &exists) != 0) {
        LOG_FATAL("worker.c", "FIXME: malformed RSP_MARK frame");
    }
    frame_free(&frame);

    return exists;
}

static void worker_write_document(document_t *doc, const char *json) {
    proto_doc_t proto_doc = {
            .mime = doc->mime,
            .mtime = doc->mtime,
            .size = (int64_t) doc->size,
            .thumbnail_count = doc->thumbnail_count,
            .json = (char *) json,
    };
    strcpy(proto_doc.path, doc->filepath);
    strcpy(proto_doc.parent, doc->parent);

    uint32_t len;
    char *payload = proto_encode_doc(&proto_doc, &len);
    send_frame(FRAME_DOC, payload, len);
}

static void worker_write_thumbnail(int index, const void *data, size_t size) {
    uint32_t len;
    char *payload = proto_encode_thumb(index, data, size, &len);
    send_frame(FRAME_THUMB, payload, len);
}

static void worker_set_current_job(const char *filepath) {
    send_frame(FRAME_CURRENT_JOB, strdup(filepath), (uint32_t) strlen(filepath));
}

static const document_sink_t WorkerSink = {
        .mark_document = worker_mark_document,
        .write_document = worker_write_document,
        .write_thumbnail = worker_write_thumbnail,
        .set_current_job = worker_set_current_job,
};

void worker_run() {
    DocumentSink = &WorkerSink;

    while (TRUE) {
        frame_t frame;

        int ret = frame_read(WORKER_IN_FD, &frame);
        if (ret != 0) {
            // Clean EOF means the master exited without saying goodbye; either way we are done
            break;
        }

        if (frame.type == FRAME_BYE) {
            frame_free(&frame);
            break;
        }

        if (frame.type != FRAME_JOB) {
            LOG_FATALF("worker.c", "FIXME: expected JOB, got frame type %d", frame.type);
        }

        proto_job_t job;
        if (proto_decode_job(&frame, &job) != 0) {
            LOG_FATAL("worker.c", "FIXME: malformed JOB frame");
        }
        frame_free(&frame);

        parse_job_t *parse_job = create_parse_job(job.path, job.mtime, job.size);
        parse(parse_job);
        free(parse_job);

        send_frame(FRAME_DONE, NULL, 0);
    }
}
