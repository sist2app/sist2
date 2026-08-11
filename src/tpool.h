#ifndef SIST2_TPOOL_H
#define SIST2_TPOOL_H

#include "sist.h"
#include "libscan/scan.h"
#include "index/elastic.h"
#include "src/database/database.h"

struct tpool;
typedef struct tpool tpool_t;

tpool_t *tpool_create(int num, int print_progress);

void tpool_start(tpool_t *pool);

void tpool_destroy(tpool_t *pool);

int tpool_add_work(tpool_t *pool, job_t *job);

void tpool_wait(tpool_t *pool);

void job_destroy(job_t *job);

#endif


