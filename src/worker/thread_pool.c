#include "thread_pool.h"

#include "src/ctx.h"

#include <pthread.h>

// Parsers run on the worker stack; musl's 128kB default is not enough
#define WORKER_STACK_SIZE (16 * 1024 * 1024)

#define QUEUE_CAPACITY_PER_THREAD (8)
#define MIN_QUEUE_CAPACITY (32)

typedef struct thread_pool thread_pool_t;

typedef struct {
    int thread_id;
    thread_pool_t *pool;
} worker_arg_t;

typedef struct thread_pool {
    pthread_t threads[MAX_THREADS];
    worker_arg_t worker_args[MAX_THREADS];
    int thread_count;

    int print_progress;
    thread_pool_job_func_t job_func;
    thread_pool_hook_t on_thread_start;
    thread_pool_hook_t on_thread_exit;

    queue_t *queue;

    pthread_mutex_t counter_mutex;
    size_t submitted_count;
    size_t completed_count;
    int waiting;
} thread_pool_t;

thread_pool_t *thread_pool_create(thread_pool_options_t options) {
    if (options.thread_count <= 0 || options.thread_count > MAX_THREADS) {
        LOG_FATALF("thread_pool.c", "Invalid thread count: %d", options.thread_count);
    }

    thread_pool_t *pool = calloc(1, sizeof(thread_pool_t));

    pool->thread_count = options.thread_count;
    pool->print_progress = options.print_progress;
    pool->job_func = options.job_func;
    pool->on_thread_start = options.on_thread_start;
    pool->on_thread_exit = options.on_thread_exit;

    pool->queue = queue_create(MAX(options.thread_count * QUEUE_CAPACITY_PER_THREAD, MIN_QUEUE_CAPACITY));

    pthread_mutex_init(&pool->counter_mutex, NULL);

    return pool;
}

static void print_progress(thread_pool_t *pool) {
    pthread_mutex_lock(&pool->counter_mutex);
    size_t done = pool->completed_count;
    size_t count = pool->submitted_count;
    int waiting = pool->waiting;
    pthread_mutex_unlock(&pool->counter_mutex);

    if (LogCtx.json_logs) {
        progress_bar_print_json(done, count, 0, 0, waiting);
    } else {
        progress_bar_print(count == 0 ? 1.0 : (double) done / (double) count, 0, 0);
    }
}

static void *thread_pool_worker(void *arg) {
    int thread_id = ((worker_arg_t *) arg)->thread_id;
    thread_pool_t *pool = ((worker_arg_t *) arg)->pool;

    ProcData.thread_id = thread_id;

    if (pool->on_thread_start != NULL) {
        pool->on_thread_start(thread_id);
    }

    void *job;
    while ((job = queue_pop(pool->queue)) != NULL) {
        pool->job_func(job);

        pthread_mutex_lock(&pool->counter_mutex);
        pool->completed_count += 1;
        pthread_mutex_unlock(&pool->counter_mutex);

        if (pool->print_progress) {
            print_progress(pool);
        }
    }

    if (pool->on_thread_exit != NULL) {
        pool->on_thread_exit(thread_id);
    }

    return NULL;
}

void thread_pool_start(thread_pool_t *pool) {
    LOG_INFOF("thread_pool.c", "Starting thread pool with %d threads", pool->thread_count);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, WORKER_STACK_SIZE);

    for (int i = 0; i < pool->thread_count; i++) {
        pool->worker_args[i] = (worker_arg_t) {.thread_id = i + 1, .pool = pool};
        pthread_create(&pool->threads[i], &attr, thread_pool_worker, &pool->worker_args[i]);
    }

    pthread_attr_destroy(&attr);
}

void thread_pool_submit(thread_pool_t *pool, void *job) {
    pthread_mutex_lock(&pool->counter_mutex);
    pool->submitted_count += 1;
    pthread_mutex_unlock(&pool->counter_mutex);

    if (!queue_push(pool->queue, job)) {
        LOG_FATAL("thread_pool.c", "FIXME: submitted a job to a closed thread pool");
    }
}

void thread_pool_wait(thread_pool_t *pool) {
    LOG_DEBUG("thread_pool.c", "Waiting for worker threads to finish");

    pthread_mutex_lock(&pool->counter_mutex);
    pool->waiting = TRUE;
    pthread_mutex_unlock(&pool->counter_mutex);

    queue_close(pool->queue);

    for (int i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }

    if (pool->print_progress && !LogCtx.json_logs) {
        progress_bar_print(1.0, 0, 0);
    }

    LOG_INFO("thread_pool.c", "Worker threads finished");
}

void thread_pool_destroy(thread_pool_t *pool) {
    queue_destroy(pool->queue);
    pthread_mutex_destroy(&pool->counter_mutex);

    free(pool);
}
