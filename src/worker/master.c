#include "master.h"

#include "protocol.h"
#include "queue.h"
#include "worker.h"
#include "src/ctx.h"

#include <pthread.h>
#include <uv.h>

#define BLANK_STR "                                         "

/**
 * How far the walk is allowed to run ahead of the workers.
 *
 * The bound is what keeps a scan of millions of files from buffering all of their paths, but it
 * cannot be tight: the progress bar shows completed/submitted, so a walk kept on a short leash makes
 * every scan look like it is permanently at 99%. This is deep enough that the walk finishes well
 * before the workers on any normal index, which is what makes the ratio mean something, and it costs
 * about 10MB of queued paths — the same order as the /dev/shm job database it replaced.
 */
#define QUEUE_CAPACITY (50000)

// process, the two pipes and the deadline timer
#define HANDLES_PER_WORKER (4)

// musl's 128kB default is not enough for the recursive walk
#define PRODUCER_STACK_SIZE (16 * 1024 * 1024)

typedef struct {
    char *path;
    int mtime;
    int64_t size;
} scan_job_t;

static void scan_job_destroy(scan_job_t *job) {
    free(job->path);
    free(job);
}

typedef struct worker {
    scan_master_t *master;

    uv_process_t process;
    /** Master's end of the pipe the worker reads from */
    uv_pipe_t in;
    /** Master's end of the pipe the worker writes to */
    uv_pipe_t out;
    /** Only armed when --job-timeout is set */
    uv_timer_t deadline;
    frame_parser_t *parser;

    int alive;
    int busy;
    int said_bye;
    int timed_out;
    int open_handles;
    int64_t exit_status;
    int term_signal;
    /** Jobs this slot finished, across respawns */
    size_t completed_jobs;

    /** The job the worker was given, and the archive member it reported working on */
    char job_path[SIST_PATH_MAX];
    char current_path[SIST_PATH_MAX];

    /** Document the next THUMB frames belong to */
    int last_doc_id;
} worker_t;

typedef struct scan_master {
    uv_loop_t loop;
    uv_async_t wakeup;

    worker_t workers[MAX_THREADS];
    int worker_count;
    int live_workers;

    queue_t *queue;

    int print_progress;
    /** Written by the producer thread, read by the loop thread */
    pthread_mutex_t counter_mutex;
    size_t submitted_count;
    size_t completed_count;
    /** Set once the producer is done, so the progress bar can show a total instead of a moving one */
    int producer_finished;
} scan_master_t;

static void spawn_worker(worker_t *worker);

static void dispatch(scan_master_t *master);

static void arm_deadline(worker_t *worker);

static void disarm_deadline(worker_t *worker);

/* Writing to a worker */

typedef struct {
    uv_write_t req;
    char *buf;
} write_req_t;

static void write_done(uv_write_t *req, int status) {
    write_req_t *write_req = (write_req_t *) req;

    if (status != 0 && status != UV_EPIPE && status != UV_ECANCELED) {
        LOG_WARNINGF("master.c", "Could not write to worker: %s", uv_strerror(status));
    }

    free(write_req->buf);
    free(write_req);
}

static void send_frame(worker_t *worker, uint32_t type, char *payload, uint32_t len) {
    write_req_t *write_req = malloc(sizeof(write_req_t));

    write_req->buf = malloc(FRAME_HEADER_SIZE + len);
    uint32_t header[2] = {type, len};
    memcpy(write_req->buf, header, FRAME_HEADER_SIZE);
    if (len > 0) {
        memcpy(write_req->buf + FRAME_HEADER_SIZE, payload, len);
    }
    free(payload);

    uv_buf_t buf = uv_buf_init(write_req->buf, FRAME_HEADER_SIZE + len);

    int ret = uv_write((uv_write_t *) write_req, (uv_stream_t *) &worker->in, &buf, 1, write_done);
    if (ret != 0) {
        LOG_WARNINGF("master.c", "Could not queue write to worker: %s", uv_strerror(ret));
        free(write_req->buf);
        free(write_req);
    }
}

/* Progress */

static void print_progress(scan_master_t *master) {
    if (!master->print_progress) {
        return;
    }

    pthread_mutex_lock(&master->counter_mutex);
    size_t count = master->submitted_count;
    pthread_mutex_unlock(&master->counter_mutex);

    size_t done = master->completed_count;

    if (LogCtx.json_logs) {
        progress_bar_print_json(done, count, 0, 0, master->producer_finished);
    } else {
        progress_bar_print(count == 0 ? 1.0 : (double) done / (double) count, 0, 0);
    }
}

/* Frames coming back from a worker */

static void handle_doc(worker_t *worker, const frame_t *frame) {
    proto_doc_t proto_doc;

    if (proto_decode_doc(frame, &proto_doc) != 0) {
        LOG_FATAL("master.c", "FIXME: malformed DOC frame");
    }

    document_t doc = {
            .size = proto_doc.size,
            .mime = proto_doc.mime,
            .mtime = proto_doc.mtime,
            .thumbnail_count = proto_doc.thumbnail_count,
    };
    strcpy(doc.filepath, proto_doc.path);
    strcpy(doc.parent, proto_doc.parent);

    worker->last_doc_id = database_write_document(ProcData.index_db, &doc, proto_doc.json);

    free(proto_doc.json);
}

static void handle_thumb(worker_t *worker, const frame_t *frame) {
    proto_thumb_t thumb;

    if (proto_decode_thumb(frame, &thumb) != 0) {
        LOG_FATAL("master.c", "FIXME: malformed THUMB frame");
    }

    database_write_thumbnail(ProcData.index_db, worker->last_doc_id, thumb.index,
                             (void *) thumb.data, thumb.size);
}

static void handle_req_mark(worker_t *worker, const frame_t *frame) {
    proto_mark_t mark;

    if (proto_decode_mark(frame, &mark) != 0) {
        LOG_FATAL("master.c", "FIXME: malformed REQ_MARK frame");
    }

    int exists = database_mark_document(ProcData.index_db, mark.path, mark.mtime);

    uint32_t len;
    char *payload = proto_encode_i32(exists, &len);
    send_frame(worker, FRAME_RSP_MARK, payload, len);
}

static void handle_frame(const frame_t *frame, void *user_data) {
    worker_t *worker = user_data;

    switch (frame->type) {
        case FRAME_DOC:
            handle_doc(worker, frame);
            break;
        case FRAME_THUMB:
            handle_thumb(worker, frame);
            break;
        case FRAME_REQ_MARK:
            handle_req_mark(worker, frame);
            break;
        case FRAME_CURRENT_JOB:
            memcpy(worker->current_path, frame->payload, MIN(frame->len, sizeof(worker->current_path) - 1));
            worker->current_path[MIN(frame->len, sizeof(worker->current_path) - 1)] = '\0';
            // Per file, not per job: an archive re-arms it on every member
            arm_deadline(worker);
            break;
        case FRAME_DONE:
            disarm_deadline(worker);
            worker->busy = FALSE;
            worker->completed_jobs += 1;
            worker->job_path[0] = '\0';
            worker->current_path[0] = '\0';
            worker->master->completed_count += 1;
            print_progress(worker->master);
            dispatch(worker->master);
            break;
        default:
        LOG_FATALF("master.c", "FIXME: unexpected frame type from worker: %d", frame->type);
    }
}

static void alloc_read_buffer(UNUSED(uv_handle_t *handle), size_t suggested_size, uv_buf_t *buf) {
    *buf = uv_buf_init(malloc(suggested_size), (unsigned int) suggested_size);
}

static void on_worker_output(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    worker_t *worker = stream->data;

    if (nread > 0) {
        if (frame_parser_feed(worker->parser, buf->base, nread, handle_frame, worker) != 0) {
            LOG_FATAL("master.c", "FIXME: worker sent a malformed frame");
        }
    } else if (nread < 0) {
        // EOF or a broken pipe: the worker is on its way out, exit_cb decides what to do about it
        uv_read_stop(stream);
    }

    free(buf->base);
}

/* Deadlines */

static void on_deadline(uv_timer_t *timer) {
    worker_t *worker = timer->data;

    LOG_WARNINGF("master.c", "Worker exceeded the %d second job timeout on %s, restarting it",
                 ScanCtx.job_timeout,
                 worker->current_path[0] != '\0' ? worker->current_path : worker->job_path);

    // Killing it takes the usual crash path: the job is written off and a new process takes over
    worker->timed_out = TRUE;
    uv_process_kill(&worker->process, SIGKILL);
}

static void arm_deadline(worker_t *worker) {
    if (ScanCtx.job_timeout <= 0) {
        return;
    }

    uv_timer_start(&worker->deadline, on_deadline, (uint64_t) ScanCtx.job_timeout * 1000, 0);
}

static void disarm_deadline(worker_t *worker) {
    if (ScanCtx.job_timeout <= 0) {
        return;
    }

    uv_timer_stop(&worker->deadline);
}

/* Spawning, crashes and respawns */

static void report_crash(worker_t *worker) {
    if (worker->timed_out) {
        // on_deadline() already said what happened
        return;
    }

    const char *job_filepath = worker->current_path[0] != '\0'
                               ? worker->current_path
                               : (worker->job_path[0] != '\0' ? worker->job_path : "unknown");

    if (worker->term_signal != 0) {
        LOG_FATALF_NO_EXIT(
                "master.c",
                "Worker process crashed (%s).\n"
                BLANK_STR "The process was working on %s\n"
                BLANK_STR "Please consider creating a bug report at https://github.com/simon987/sist2/issues !\n"
                BLANK_STR "sist2 is an open source project and relies on the collaboration of its users to diagnose and fix bugs.\n",
                strsignal(worker->term_signal),
                job_filepath
        );
    } else {
        LOG_FATALF_NO_EXIT(
                "master.c",
                "Worker process exited with status %d while working on %s\n",
                (int) worker->exit_status, job_filepath
        );
    }
}

static void on_handle_closed(uv_handle_t *handle) {
    worker_t *worker = handle->data;

    worker->open_handles -= 1;
    if (worker->open_handles > 0) {
        return;
    }

    frame_parser_destroy(worker->parser);
    worker->parser = NULL;

    // Once a worker has been told to go away, how it exits is none of our business: a debug build
    // exits non-zero whenever LeakSanitizer has something to say, and that is not a crash.
    // Anything else that ends a busy process loses its file, a plain exit(0) included.
    int crashed = !worker->said_bye
                  && (worker->timed_out || worker->term_signal != 0 || worker->exit_status != 0
                      || worker->busy);

    if (!crashed) {
        worker->alive = FALSE;
        worker->master->live_workers -= 1;

        if (worker->master->live_workers == 0) {
            // Nothing left to take work; let the producer out of queue_push()
            queue_close(worker->master->queue);
        }
        return;
    }

    report_crash(worker);

    if (!worker->busy && worker->completed_jobs == 0) {
        // Never ran a job; respawning would only spin
        LOG_FATAL("master.c", "Worker process died before it completed any work, giving up");
    }

    if (worker->busy) {
        // The job died with the process; count it so the progress bar can still reach 100%
        worker->busy = FALSE;
        worker->master->completed_count += 1;
    }

    spawn_worker(worker);
    dispatch(worker->master);
}

static void close_worker_handles(worker_t *worker) {
    uv_handle_t *handles[HANDLES_PER_WORKER] = {
            (uv_handle_t *) &worker->process,
            (uv_handle_t *) &worker->in,
            (uv_handle_t *) &worker->out,
            (uv_handle_t *) &worker->deadline,
    };

    for (int i = 0; i < HANDLES_PER_WORKER; i++) {
        if (!uv_is_closing(handles[i])) {
            uv_close(handles[i], on_handle_closed);
        }
    }
}

static void on_worker_exit(uv_process_t *process, int64_t exit_status, int term_signal) {
    worker_t *worker = process->data;

    worker->exit_status = exit_status;
    worker->term_signal = term_signal;
    // The handles close later; dispatch() must not pick this worker until then
    worker->alive = FALSE;

    LOG_DEBUGF("master.c", "Worker process terminated with status code %d", (int) exit_status);

    close_worker_handles(worker);
}

/**
 * The worker runs the same executable with the same arguments, plus --worker. It rebuilds its scan
 * context by parsing them again rather than having it serialized over the pipe.
 */
static char **build_worker_args() {
    char **args = malloc(sizeof(char *) * (ScanCtx.argc + 2));

    // In front of the user's argv: after their own "--" it would be a positional
    args[0] = (char *) ScanCtx.argv[0];
    args[1] = "--worker";
    for (int i = 1; i < ScanCtx.argc; i++) {
        args[i + 1] = (char *) ScanCtx.argv[i];
    }
    args[ScanCtx.argc + 1] = NULL;

    return args;
}

static void spawn_worker(worker_t *worker) {
    scan_master_t *master = worker->master;

    uv_pipe_init(&master->loop, &worker->in, 0);
    uv_pipe_init(&master->loop, &worker->out, 0);
    uv_timer_init(&master->loop, &worker->deadline);

    worker->process.data = worker;
    worker->in.data = worker;
    worker->out.data = worker;
    worker->deadline.data = worker;
    worker->parser = frame_parser_create();
    worker->open_handles = HANDLES_PER_WORKER;
    worker->busy = FALSE;
    worker->said_bye = FALSE;
    worker->timed_out = FALSE;
    worker->exit_status = 0;
    worker->term_signal = 0;
    worker->job_path[0] = '\0';
    worker->current_path[0] = '\0';

    uv_stdio_container_t stdio[5];
    // stdin/stdout/stderr are inherited so that log lines, and anything a parser library decides to
    // print, go to the terminal instead of into the frame stream
    for (int fd = 0; fd < 3; fd++) {
        stdio[fd].flags = UV_INHERIT_FD;
        stdio[fd].data.fd = fd;
    }
    stdio[WORKER_IN_FD].flags = UV_CREATE_PIPE | UV_READABLE_PIPE;
    stdio[WORKER_IN_FD].data.stream = (uv_stream_t *) &worker->in;
    stdio[WORKER_OUT_FD].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
    stdio[WORKER_OUT_FD].data.stream = (uv_stream_t *) &worker->out;

    char **args = build_worker_args();

    uv_process_options_t options = {
            .file = args[0],
            .args = args,
            .stdio = stdio,
            .stdio_count = 5,
            .exit_cb = on_worker_exit,
    };

    int ret = uv_spawn(&master->loop, &worker->process, &options);
    free(args);

    if (ret != 0) {
        LOG_FATALF("master.c", "Could not spawn worker process: %s", uv_strerror(ret));
    }

    worker->alive = TRUE;
    uv_read_start((uv_stream_t *) &worker->out, alloc_read_buffer, on_worker_output);
}

/* Job dispatch */

static void dispatch(scan_master_t *master) {
    for (int i = 0; i < master->worker_count; i++) {
        worker_t *worker = &master->workers[i];

        if (!worker->alive || worker->busy || worker->said_bye) {
            continue;
        }

        scan_job_t *job;
        queue_poll_result_t result = queue_poll(master->queue, (void **) &job);

        if (result == QUEUE_DONE) {
            worker->said_bye = TRUE;
            send_frame(worker, FRAME_BYE, NULL, 0);
            continue;
        }

        if (result == QUEUE_EMPTY) {
            // Nothing to hand out right now; the producer will wake us up
            return;
        }

        proto_job_t proto_job = {.mtime = job->mtime, .size = job->size};
        strcpy(proto_job.path, job->path);

        uint32_t len;
        char *payload = proto_encode_job(&proto_job, &len);

        worker->busy = TRUE;
        strcpy(worker->job_path, job->path);
        worker->current_path[0] = '\0';
        scan_job_destroy(job);

        send_frame(worker, FRAME_JOB, payload, len);
        arm_deadline(worker);
    }
}

static void on_wakeup(uv_async_t *handle) {
    dispatch(handle->data);
}

/* Producer thread */

typedef struct {
    scan_master_t *master;
    scan_producer_t producer;
    void *user_data;
    int result;
} producer_arg_t;

static void *run_producer(void *arg) {
    producer_arg_t *producer_arg = arg;

    producer_arg->result = producer_arg->producer(producer_arg->user_data);

    producer_arg->master->producer_finished = TRUE;
    queue_close(producer_arg->master->queue);
    uv_async_send(&producer_arg->master->wakeup);

    return NULL;
}

/* Public interface */

scan_master_t *scan_master_create(int worker_count, int print_progress) {
    if (worker_count <= 0 || worker_count > MAX_THREADS) {
        LOG_FATALF("master.c", "Invalid worker count: %d", worker_count);
    }

    scan_master_t *master = calloc(1, sizeof(scan_master_t));

    master->worker_count = worker_count;
    master->print_progress = print_progress;
    master->queue = queue_create(QUEUE_CAPACITY);

    pthread_mutex_init(&master->counter_mutex, NULL);

    uv_loop_init(&master->loop);
    uv_async_init(&master->loop, &master->wakeup, on_wakeup);
    master->wakeup.data = master;
    // The worker handles keep the loop running. Closed after the producer is joined, so
    // uv_async_send() cannot race with its close.
    uv_unref((uv_handle_t *) &master->wakeup);

    // This thread is the only writer the index database ever sees
    ProcData.index_db = database_create(ScanCtx.index.path, INDEX_DATABASE);
    database_open(ProcData.index_db);

    return master;
}

void scan_master_submit(scan_master_t *master, const char *path, int mtime, int64_t size) {
    scan_job_t *job = malloc(sizeof(scan_job_t));

    job->path = strdup(path);
    job->mtime = mtime;
    job->size = size;

    pthread_mutex_lock(&master->counter_mutex);
    master->submitted_count += 1;
    pthread_mutex_unlock(&master->counter_mutex);

    if (!queue_push(master->queue, job)) {
        scan_job_destroy(job);
        return;
    }

    uv_async_send(&master->wakeup);
}

int scan_master_run(scan_master_t *master, scan_producer_t producer, void *user_data) {
    LOG_INFOF("master.c", "Starting %d worker processes", master->worker_count);

    for (int i = 0; i < master->worker_count; i++) {
        master->workers[i].master = master;
        spawn_worker(&master->workers[i]);
        master->live_workers += 1;
    }

    producer_arg_t producer_arg = {.master = master, .producer = producer, .user_data = user_data};

    pthread_attr_t producer_attr;
    pthread_attr_init(&producer_attr);
    pthread_attr_setstacksize(&producer_attr, PRODUCER_STACK_SIZE);

    pthread_t producer_thread;
    pthread_create(&producer_thread, &producer_attr, run_producer, &producer_arg);
    pthread_attr_destroy(&producer_attr);

    dispatch(master);
    uv_run(&master->loop, UV_RUN_DEFAULT);

    pthread_join(producer_thread, NULL);

    uv_close((uv_handle_t *) &master->wakeup, NULL);
    uv_run(&master->loop, UV_RUN_DEFAULT);

    if (master->print_progress && !LogCtx.json_logs) {
        progress_bar_print(1.0, 0, 0);
    }

    LOG_INFO("master.c", "Worker processes finished");

    return producer_arg.result;
}

void scan_master_destroy(scan_master_t *master) {
    database_close(ProcData.index_db, FALSE);
    ProcData.index_db = NULL;

    uv_loop_close(&master->loop);

    pthread_mutex_destroy(&master->counter_mutex);

    // Anything still queued was never handed out (every worker died); free it rather than leak it
    scan_job_t *job;
    while ((job = queue_try_pop(master->queue)) != NULL) {
        scan_job_destroy(job);
    }
    queue_destroy(master->queue);

    free(master);
}
