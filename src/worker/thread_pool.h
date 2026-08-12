#ifndef SIST2_THREAD_POOL_H
#define SIST2_THREAD_POOL_H

#include "src/sist.h"
#include "src/worker/queue.h"

/**
 * In-process pool of worker threads pulling from a bounded queue.
 *
 * Used for work that does not need crash isolation (Elasticsearch bulk lines), and as the scan
 * backend in debug builds so that the sanitizers see a single address space.
 */
struct thread_pool;
typedef struct thread_pool thread_pool_t;

/**
 * Called on a worker thread for every submitted job. Takes ownership of the job.
 */
typedef void (*thread_pool_job_func_t)(void *job);

/**
 * Called on the worker thread itself, before the first job and after the last one.
 */
typedef void (*thread_pool_hook_t)(int thread_id);

typedef struct {
    int thread_count;
    int print_progress;
    thread_pool_job_func_t job_func;
    thread_pool_hook_t on_thread_start;
    thread_pool_hook_t on_thread_exit;
} thread_pool_options_t;

thread_pool_t *thread_pool_create(thread_pool_options_t options);

void thread_pool_start(thread_pool_t *pool);

/**
 * Hands the job over to the pool. Blocks while the queue is full.
 */
void thread_pool_submit(thread_pool_t *pool, void *job);

/**
 * Closes the queue and joins every worker thread.
 */
void thread_pool_wait(thread_pool_t *pool);

void thread_pool_destroy(thread_pool_t *pool);

#endif
