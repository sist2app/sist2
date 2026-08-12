#include "tpool.h"
#include "ctx.h"
#include "sist.h"
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include "parsing/parse.h"
#include "worker/sink.h"

#define BLANK_STR "                                         "

#define WORKER_STACK_SIZE (16 * 1024 * 1024)

typedef struct {
    int thread_id;
    tpool_t *pool;
} start_thread_arg_t;


typedef struct tpool {
    pthread_t threads[256];
    void *start_thread_args[256];
    int num_threads;

    int print_progress;

    struct {
        job_type_t job_type;
        int stop;
        int waiting;
        database_ipc_ctx_t ipc_ctx;
        pthread_mutex_t mutex;
        pthread_mutex_t data_mutex;
        pthread_cond_t done_working_cond;
        pthread_cond_t workers_initialized_cond;
        int busy_count;
        int initialized_count;
        int thread_id_to_pid_mapping[MAX_THREADS];
        char ipc_database_filepath[128];
    } *shm;
} tpool_t;

void job_destroy(job_t *job) {
    if (job->type == JOB_PARSE_JOB) {
        free(job->parse_job);
    }

    free(job);
}

/**
 * Push work object to thread pool
 */
int tpool_add_work(tpool_t *pool, job_t *job) {

    if (pool->shm->job_type == JOB_UNDEFINED) {
        pool->shm->job_type = job->type;
    } else if (pool->shm->job_type != job->type) {
        LOG_FATAL("tpool.c", "FIXME: tpool cannot queue jobs with different types!");
    }

    database_add_work(ProcData.ipc_db, job);

    return TRUE;
}

static void worker_thread_loop(tpool_t *pool) {
    while (TRUE) {
        if (pool->shm->stop) {
            break;
        }

        if (pool->shm->job_type == JOB_UNDEFINED) {
            // Wait before first job is queued
            pthread_mutex_lock(&pool->shm->mutex);
            pthread_cond_timedwait_ms(&pool->shm->ipc_ctx.has_work_cond, &pool->shm->mutex, 1000);
            pthread_mutex_unlock(&pool->shm->mutex);
        }

        job_t *job = database_get_work(ProcData.ipc_db, pool->shm->job_type);

        if (job != NULL) {
            if (pool->shm->stop) {
                break;
            }

            pthread_mutex_lock(&(pool->shm->data_mutex));
            pool->shm->busy_count += 1;
            pthread_mutex_unlock(&(pool->shm->data_mutex));

            if (job->type == JOB_PARSE_JOB) {
                parse(job->parse_job);
            } else if (job->type == JOB_BULK_LINE) {
                elastic_index_line(job->bulk_line);
            }

            job_destroy(job);

            pthread_mutex_lock(&(pool->shm->data_mutex));
            pool->shm->busy_count -= 1;
            pthread_mutex_unlock(&(pool->shm->data_mutex));

            pthread_mutex_lock(&(pool->shm->ipc_ctx.mutex));
            pool->shm->ipc_ctx.completed_job_count += 1;
            pthread_mutex_unlock(&(pool->shm->ipc_ctx.mutex));
        }

        if (pool->print_progress) {

            int done = pool->shm->ipc_ctx.completed_job_count;
            int count = pool->shm->ipc_ctx.completed_job_count + pool->shm->ipc_ctx.job_count;

            if (LogCtx.json_logs) {
                progress_bar_print_json(done,
                                        count,
                                        0,
                                        0, pool->shm->waiting);
            } else {
                progress_bar_print((double) done / count,
                                   0, 0);
            }
        }

        if (job == NULL) {
            pthread_mutex_lock(&pool->shm->mutex);
            pthread_cond_signal(&pool->shm->done_working_cond);
            pthread_mutex_unlock(&pool->shm->mutex);
        }
    }
}

static void worker_proc_init(tpool_t *pool, int thread_id) {
    pthread_mutex_lock(&pool->shm->data_mutex);
    pool->shm->thread_id_to_pid_mapping[thread_id] = getpid();
    pthread_mutex_unlock(&pool->shm->data_mutex);

    ProcData.thread_id = thread_id;
    DocumentSink = &DatabaseSink;

    if (ScanCtx.index.path[0] != '\0') {
        ProcData.index_db = database_create(ScanCtx.index.path, INDEX_DATABASE);
        ProcData.index_db->ipc_ctx = &pool->shm->ipc_ctx;
        database_open(ProcData.index_db);
    }

    pthread_mutex_lock(&pool->shm->mutex);
    ProcData.ipc_db = database_create(pool->shm->ipc_database_filepath, IPC_CONSUMER_DATABASE);
    ProcData.ipc_db->ipc_ctx = &pool->shm->ipc_ctx;
    database_open(ProcData.ipc_db);
    pthread_mutex_unlock(&pool->shm->mutex);
}

void worker_proc_cleanup(UNUSED(tpool_t *pool)) {
    if (ProcData.index_db != NULL) {
        database_close(ProcData.index_db, FALSE);
    }

    if (IndexCtx.needs_es_connection) {
        elastic_cleanup();
    }

    database_close(ProcData.ipc_db, FALSE);
}

#ifndef SIST_DEBUG
#define TPOOL_FORK
#endif

/**
 * Thread worker function
 */
static void *tpool_worker(void *arg) {
    tpool_t *pool = ((start_thread_arg_t *) arg)->pool;

#ifdef TPOOL_FORK
    while (TRUE) {
        int pid = fork();

        if (pid == 0) {
            worker_proc_init(pool, ((start_thread_arg_t *) arg)->thread_id);

            pthread_mutex_lock(&pool->shm->mutex);
            pthread_cond_signal(&pool->shm->workers_initialized_cond);
            pool->shm->initialized_count += 1;
            pthread_mutex_unlock(&pool->shm->mutex);

            worker_thread_loop(pool);

            pthread_mutex_lock(&pool->shm->mutex);
            pthread_cond_signal(&pool->shm->done_working_cond);
            pthread_mutex_unlock(&pool->shm->mutex);

            worker_proc_cleanup(pool);

            exit(0);

        } else {
            int status;
            waitpid(pid, &status, 0);

            LOG_DEBUGF("tpool.c", "Child process terminated with status code %d", WEXITSTATUS(status));

            pthread_mutex_lock(&(pool->shm->ipc_ctx.mutex));
            pool->shm->ipc_ctx.completed_job_count += 1;
            pthread_mutex_unlock(&(pool->shm->ipc_ctx.mutex));

            if (WIFSIGNALED(status)) {
                pthread_mutex_lock(&(pool->shm->data_mutex));
                pool->shm->busy_count -= 1;
                pthread_mutex_unlock(&(pool->shm->data_mutex));

                int crashed_thread_id = -1;
                for (int i = 0; i < MAX_THREADS; i++) {
                    if (pool->shm->thread_id_to_pid_mapping[i] == pid) {
                        crashed_thread_id = i;
                        break;
                    }
                }

                const char *job_filepath;
                if (crashed_thread_id != -1) {
                    job_filepath = pool->shm->ipc_ctx.current_job[crashed_thread_id];
                } else {
                    job_filepath = "unknown";
                }

                LOG_FATALF_NO_EXIT(
                        "tpool.c",
                        "Child process crashed (%s).\n"
                        BLANK_STR "The process was working on %s\n"
                        BLANK_STR "Please consider creating a bug report at https://github.com/simon987/sist2/issues !\n"
                        BLANK_STR "sist2 is an open source project and relies on the collaboration of its users to diagnose and fix bugs.\n",
                        strsignal(WTERMSIG(status)),
                        job_filepath
                );
                continue;
            }
            break;
        }
    }

#else
    worker_proc_init(pool, ((start_thread_arg_t *) arg)->thread_id);

    pthread_mutex_lock(&pool->shm->mutex);
    pthread_cond_signal(&pool->shm->workers_initialized_cond);
    pool->shm->initialized_count += 1;
    pthread_mutex_unlock(&pool->shm->mutex);

    worker_thread_loop(pool);

    pthread_mutex_lock(&pool->shm->mutex);
    pthread_cond_signal(&pool->shm->done_working_cond);
    pthread_mutex_unlock(&pool->shm->mutex);
    worker_proc_cleanup(pool);
#endif

    return NULL;
}

void tpool_wait(tpool_t *pool) {
    LOG_DEBUG("tpool.c", "Waiting for worker threads to finish");
    pthread_mutex_lock(&pool->shm->mutex);

    pool->shm->waiting = TRUE;
    pool->shm->ipc_ctx.no_more_jobs = TRUE;

    while (TRUE) {
        if (pool->shm->ipc_ctx.job_count > 0) {
            pthread_cond_wait(&(pool->shm->done_working_cond), &pool->shm->mutex);
        } else {
            if (pool->shm->ipc_ctx.job_count == 0 && pool->shm->busy_count <= 0) {
                pool->shm->stop = TRUE;
                break;
            }
        }
    }
    if (pool->print_progress && !LogCtx.json_logs) {
        progress_bar_print(1.0, 0, 0);
    }
    pthread_mutex_unlock(&pool->shm->mutex);

    LOG_INFO("tpool.c", "Worker threads finished");
}

void tpool_destroy(tpool_t *pool) {
    LOG_INFO("tpool.c", "Destroying thread pool");

    database_close(ProcData.ipc_db, FALSE);

    pthread_mutex_lock(&pool->shm->mutex);
    pthread_cond_broadcast(&pool->shm->ipc_ctx.has_work_cond);
    pthread_mutex_unlock(&pool->shm->mutex);

    for (int i = 0; i < pool->num_threads; i++) {
        pthread_t thread = pool->threads[i];
        if (thread != 0) {
            void *_;
            pthread_join(thread, &_);
        }

        free(pool->start_thread_args[i]);
    }

    pthread_mutex_destroy(&pool->shm->ipc_ctx.mutex);
    pthread_mutex_destroy(&pool->shm->mutex);
    pthread_cond_destroy(&pool->shm->ipc_ctx.has_work_cond);
    pthread_cond_destroy(&pool->shm->done_working_cond);

    munmap(pool->shm, sizeof(*pool->shm));
}

/**
 * Create a thread pool
 * @param thread_cnt Worker threads count
 */
tpool_t *tpool_create(int thread_cnt, int print_progress) {

    tpool_t *pool = malloc(sizeof(tpool_t));

    pool->shm = mmap(NULL, sizeof(*pool->shm), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);

    pool->num_threads = thread_cnt;
    pool->shm->ipc_ctx.job_count = 0;
    pool->shm->ipc_ctx.no_more_jobs = FALSE;
    pool->shm->stop = FALSE;
    pool->shm->waiting = FALSE;
    pool->shm->job_type = JOB_UNDEFINED;
    memset(pool->threads, 0, sizeof(pool->threads));
    memset(pool->start_thread_args, 0, sizeof(pool->start_thread_args));
    pool->print_progress = print_progress;
    sprintf(pool->shm->ipc_database_filepath, "/dev/shm/sist2-ipc-%d.sqlite", getpid());

    pthread_mutexattr_t mutexattr;
    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setpshared(&mutexattr, TRUE);

    pthread_mutex_init(&(pool->shm->mutex), &mutexattr);
    pthread_mutex_init(&(pool->shm->data_mutex), &mutexattr);
    pthread_mutex_init(&(pool->shm->ipc_ctx.mutex), &mutexattr);
    pthread_mutex_init(&(pool->shm->ipc_ctx.db_mutex), &mutexattr);
    pthread_mutex_init(&(pool->shm->ipc_ctx.index_db_mutex), &mutexattr);

    pthread_condattr_t condattr;
    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr, TRUE);

    pthread_cond_init(&(pool->shm->ipc_ctx.has_work_cond), &condattr);
    pthread_cond_init(&(pool->shm->done_working_cond), &condattr);
    pthread_cond_init(&(pool->shm->workers_initialized_cond), &condattr);

    ProcData.ipc_db = database_create(pool->shm->ipc_database_filepath, IPC_PRODUCER_DATABASE);
    ProcData.ipc_db->ipc_ctx = &pool->shm->ipc_ctx;
    database_initialize(ProcData.ipc_db);

    return pool;
}

void tpool_start(tpool_t *pool) {

    LOG_INFOF("tpool.c", "Starting thread pool with %d threads", pool->num_threads);

    pthread_mutex_lock(&pool->shm->mutex);

    // Parsers run on the worker stack; musl's 128kB default is not enough
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, WORKER_STACK_SIZE);

    for (int i = 0; i < pool->num_threads; i++) {

        start_thread_arg_t *arg = malloc(sizeof(start_thread_arg_t));
        arg->thread_id = i + 1;
        arg->pool = pool;

        pthread_create(&pool->threads[i], &attr, tpool_worker, arg);
        pool->start_thread_args[i] = arg;
    }

    pthread_attr_destroy(&attr);

    // Only open the database when all workers are done initializing
    while (pool->shm->initialized_count != pool->num_threads) {
        pthread_cond_wait(&pool->shm->workers_initialized_cond, &pool->shm->mutex);
    }
    pthread_mutex_unlock(&pool->shm->mutex);

    database_open(ProcData.ipc_db);
}
