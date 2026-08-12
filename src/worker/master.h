#ifndef SIST2_WORKER_MASTER_H
#define SIST2_WORKER_MASTER_H

#include "src/sist.h"

#include <stdint.h>

/**
 * Scan master: owns the job queue, the only connection to the index database, and a pool of
 * long-lived `sist2 scan --worker` processes.
 *
 * Workers are spawned once and handle jobs until the scan is over; a new process is only spawned to
 * replace one that crashed or was killed for running past its deadline.
 */
struct scan_master;
typedef struct scan_master scan_master_t;

/**
 * Produces the jobs of a scan, by calling scan_master_submit(). Runs on its own thread.
 */
typedef int (*scan_producer_t)(void *user_data);

scan_master_t *scan_master_create(int worker_count, int print_progress);

/**
 * Runs the producer and the event loop until every submitted job is done.
 * @return the producer's return value.
 */
int scan_master_run(scan_master_t *master, scan_producer_t producer, void *user_data);

/**
 * Called from the producer thread. Blocks while the queue is full, which is what keeps the walk
 * from running arbitrarily far ahead of the workers.
 */
void scan_master_submit(scan_master_t *master, const char *path, int mtime, int64_t size);

void scan_master_destroy(scan_master_t *master);

#endif
