#include "worker.h"

#include "protocol.h"
#include "sink.h"
#include "src/ctx.h"
#include "src/parsing/parse.h"

#include <pthread.h>
#include <signal.h>

/**
 * Parsers recurse deep enough to overflow both musl's 128kB thread default and the 8MB the
 * kernel gives the main stack, so the job loop gets a stack of its own.
 */
#define PARSE_STACK_SIZE (16 * 1024 * 1024)

/** The document the next thumbnails belong to was dropped */
static int last_document_dropped = FALSE;

/**
 * @return 0 on success, -1 if the frame was too big and was dropped.
 */
static int send_frame(const uint32_t type, char *payload, const uint32_t len) {
    if (len > FRAME_MAX_PAYLOAD) {
        // Losing one document beats the master killing the scan over it
        LOG_WARNINGF("worker.c", "Dropping a %u byte frame, over the %d byte protocol limit",
                     len, FRAME_MAX_PAYLOAD);
        free(payload);
        return -1;
    }

    const int ret = frame_write(WORKER_OUT_FD, type, payload, len);
    free(payload);

    if (ret != 0) {
        // The master is gone; there is nothing left to do and nobody to report it to
        exit(0);
    }

    return 0;
}

static int worker_mark_document(const char *rel_path, const int mtime) {
    // Both buffers are SIST_PATH_MAX, and rel_path is an offset into an even shorter one
    proto_mark_t mark = {.mtime = mtime};
    strcpy(mark.path, rel_path);

    uint32_t len;
    char *payload = proto_encode_mark(&mark, &len);
    if (send_frame(FRAME_REQ_MARK, payload, len) != 0) {
        return FALSE;
    }

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
    last_document_dropped = send_frame(FRAME_DOC, payload, len) != 0;

    if (last_document_dropped) {
        LOG_WARNINGF("worker.c", "Document is too large to be indexed: %s", doc->filepath);
    }
}

static void worker_write_thumbnail(const int index, const void *data, const size_t size) {
    if (last_document_dropped) {
        // The master would attach these to the previous document
        return;
    }

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

/**
 * Test hooks: make the worker die, exit or hang on a chosen file, or delete it before parsing it.
 * They stand in for the malformed documents that make a parser segfault or spin, and for the file
 * that goes away mid-scan, which are hard to keep around as fixtures.
 */
static int triggered_by(const char *variable, const char *path) {
    const char *trigger = getenv(variable);

    return trigger != NULL && *trigger != '\0' && strstr(path, trigger) != NULL;
}

static void maybe_misbehave_for_test(const char *path) {
    if (triggered_by("SIST2_CRASH_ON_FILE", path)) {
        raise(SIGSEGV);
    }

    // A plain exit(), as a parser calling it on its own would: the job is lost either way
    if (triggered_by("SIST2_EXIT_ON_FILE", path)) {
        exit(0);
    }

    if (triggered_by("SIST2_HANG_ON_FILE", path)) {
        while (TRUE) {
            sleep(3600);
        }
    }

    // A file that is deleted after the walk queued it, as a download or a temporary file would be
    if (triggered_by("SIST2_DELETE_ON_FILE", path)) {
        unlink(path);
    }
}

static void *worker_loop(void *arg) {
    (void) arg;

    DocumentSink = &WorkerSink;

    // A master that went away should end this process through the EOF path below, not through a
    // signal raised in the middle of writing a document
    signal(SIGPIPE, SIG_IGN);

    while (TRUE) {
        frame_t frame;

        const int ret = frame_read(WORKER_IN_FD, &frame);
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

        maybe_misbehave_for_test(job.path);

        parse_job_t *parse_job = create_parse_job(job.path, job.mtime, job.size);
        parse(parse_job);
        free(parse_job);

        send_frame(FRAME_DONE, NULL, 0);
    }

    return NULL;
}

void worker_run() {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, PARSE_STACK_SIZE);

    pthread_t thread;
    if (pthread_create(&thread, &attr, worker_loop, NULL) != 0) {
        LOG_FATAL("worker.c", "Could not start the job thread");
    }
    pthread_attr_destroy(&attr);

    pthread_join(thread, NULL);
}
