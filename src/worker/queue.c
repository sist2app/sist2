#include "queue.h"

#include <pthread.h>

typedef struct queue {
    void **items;
    int capacity;
    int head;
    int tail;
    int count;
    int closed;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} queue_t;

queue_t *queue_create(int capacity) {
    if (capacity <= 0) {
        LOG_FATALF("queue.c", "Invalid queue capacity: %d", capacity);
    }

    queue_t *queue = malloc(sizeof(queue_t));

    queue->items = malloc(sizeof(void *) * capacity);
    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->closed = FALSE;

    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
    pthread_cond_init(&queue->not_full, NULL);

    return queue;
}

void queue_destroy(queue_t *queue) {
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);

    free(queue->items);
    free(queue);
}

int queue_push(queue_t *queue, void *item) {
    pthread_mutex_lock(&queue->mutex);

    while (queue->count == queue->capacity && !queue->closed) {
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }

    if (queue->closed) {
        pthread_mutex_unlock(&queue->mutex);
        return FALSE;
    }

    queue->items[queue->tail] = item;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count += 1;

    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);

    return TRUE;
}

// Caller holds the mutex and has checked that the queue is not empty
static void *queue_take(queue_t *queue) {
    void *item = queue->items[queue->head];

    queue->head = (queue->head + 1) % queue->capacity;
    queue->count -= 1;

    pthread_cond_signal(&queue->not_full);

    return item;
}

void *queue_pop(queue_t *queue) {
    pthread_mutex_lock(&queue->mutex);

    while (queue->count == 0 && !queue->closed) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    // Drain what is left before reporting the queue as finished
    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }

    void *item = queue_take(queue);
    pthread_mutex_unlock(&queue->mutex);

    return item;
}

void *queue_try_pop(queue_t *queue) {
    pthread_mutex_lock(&queue->mutex);

    if (queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }

    void *item = queue_take(queue);
    pthread_mutex_unlock(&queue->mutex);

    return item;
}

void queue_close(queue_t *queue) {
    pthread_mutex_lock(&queue->mutex);

    queue->closed = TRUE;

    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
}

int queue_is_closed(queue_t *queue) {
    pthread_mutex_lock(&queue->mutex);
    int closed = queue->closed;
    pthread_mutex_unlock(&queue->mutex);

    return closed;
}

int queue_size(queue_t *queue) {
    pthread_mutex_lock(&queue->mutex);
    int count = queue->count;
    pthread_mutex_unlock(&queue->mutex);

    return count;
}
