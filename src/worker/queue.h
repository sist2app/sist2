#ifndef SIST2_QUEUE_H
#define SIST2_QUEUE_H

#include "src/sist.h"

/**
 * Bounded blocking queue of opaque pointers.
 *
 * The bound is what provides backpressure: a producer that outruns the consumers blocks in
 * queue_push() instead of buffering an unbounded number of items.
 */
struct queue;
typedef struct queue queue_t;

queue_t *queue_create(int capacity);

void queue_destroy(queue_t *queue);

/**
 * Blocks while the queue is full.
 * @return TRUE if the item was queued, FALSE if the queue is closed (item is not consumed).
 */
int queue_push(queue_t *queue, void *item);

/**
 * Blocks while the queue is empty and open.
 * @return the next item, or NULL once the queue is closed and drained.
 */
void *queue_pop(queue_t *queue);

/**
 * @return the next item, or NULL if the queue is currently empty.
 */
void *queue_try_pop(queue_t *queue);

/**
 * Signal that no more items will be pushed. Wakes every blocked producer and consumer.
 */
void queue_close(queue_t *queue);

int queue_is_closed(queue_t *queue);

int queue_size(queue_t *queue);

#endif
