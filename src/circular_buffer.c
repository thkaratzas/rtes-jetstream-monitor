#include "circular_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void cb_init(circular_buffer_t *cb)
{
    memset(cb, 0, sizeof(*cb));
    cb->head = 0;
    cb->tail = 0;
    cb->count = 0;
    cb->shutdown = 0;
    pthread_mutex_init(&cb->mutex, NULL);
    pthread_cond_init(&cb->not_empty, NULL);
    pthread_cond_init(&cb->not_full, NULL);
}

void cb_destroy(circular_buffer_t *cb)
{
    pthread_mutex_lock(&cb->mutex);
    while (cb->count > 0) {
        free(cb->items[cb->tail].msg);
        cb->items[cb->tail].msg = NULL;
        cb->tail = (cb->tail + 1) % CB_CAPACITY;
        cb->count--;
    }
    pthread_mutex_unlock(&cb->mutex);

    pthread_mutex_destroy(&cb->mutex);
    pthread_cond_destroy(&cb->not_empty);
    pthread_cond_destroy(&cb->not_full);
}

void cb_shutdown(circular_buffer_t *cb)
{
    pthread_mutex_lock(&cb->mutex);
    cb->shutdown = 1;
    pthread_cond_broadcast(&cb->not_empty);
    pthread_cond_broadcast(&cb->not_full);
    pthread_mutex_unlock(&cb->mutex);
}

int cb_push(circular_buffer_t *cb, const char *msg, size_t len)
{
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        fprintf(stderr, "cb_push: out of memory (len=%zu)\n", len);
        return -1;
    }
    memcpy(copy, msg, len);
    copy[len] = '\0';

    pthread_mutex_lock(&cb->mutex);

    while (cb->count == CB_CAPACITY && !cb->shutdown) {
        pthread_cond_wait(&cb->not_full, &cb->mutex);
    }

    if (cb->shutdown) {
        pthread_mutex_unlock(&cb->mutex);
        free(copy);
        return -1;
    }

    cb->items[cb->head].msg = copy;
    cb->items[cb->head].len = len;
    cb->head = (cb->head + 1) % CB_CAPACITY;
    cb->count++;

    pthread_cond_signal(&cb->not_empty);
    pthread_mutex_unlock(&cb->mutex);
    return 0;
}

int cb_pop(circular_buffer_t *cb, char **out_msg, size_t *out_len)
{
    pthread_mutex_lock(&cb->mutex);

    while (cb->count == 0 && !cb->shutdown) {
        pthread_cond_wait(&cb->not_empty, &cb->mutex);
    }

    if (cb->count == 0 && cb->shutdown) {
        pthread_mutex_unlock(&cb->mutex);
        return -1;
    }

    *out_msg = cb->items[cb->tail].msg;
    *out_len = cb->items[cb->tail].len;
    cb->items[cb->tail].msg = NULL;
    cb->tail = (cb->tail + 1) % CB_CAPACITY;
    cb->count--;

    pthread_cond_signal(&cb->not_full);
    pthread_mutex_unlock(&cb->mutex);
    return 0;
}

int cb_occupancy_pct(circular_buffer_t *cb)
{
    pthread_mutex_lock(&cb->mutex);
    int count = cb->count;
    pthread_mutex_unlock(&cb->mutex);
    return (int)((100L * count) / CB_CAPACITY);
}
