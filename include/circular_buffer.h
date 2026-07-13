#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <pthread.h>
#include <stddef.h>

/*
 * Bounded Circular Queue (Producer-Consumer)
 * -------------------------------------------
 * Stores heap-allocated copies of raw JSON text frames coming from the
 * WebSocket Producer thread. Each slot holds a pointer + length, not a
 * fixed-size char array, because Jetstream messages vary a lot in size
 * (a plain "commit" post vs one with embeds/facets/reply chains).
 *
 * CAPACITY is the number of message *slots*, not bytes. Sized generously
 * because the Jetstream firehose can burst well above its average rate.
 */
#define CB_CAPACITY 2048

typedef struct {
    char   *msg;   /* heap-allocated, NUL-terminated copy of the JSON text */
    size_t  len;   /* length in bytes, not including the NUL terminator     */
} cb_item_t;

typedef struct {
    cb_item_t items[CB_CAPACITY];
    int head;                 /* index of next free slot to write into      */
    int tail;                 /* index of next occupied slot to read from   */
    int count;                /* number of occupied slots right now         */

    pthread_mutex_t mutex;
    pthread_cond_t  not_empty; /* signaled by push(), waited on by pop()    */
    pthread_cond_t  not_full;  /* signaled by pop(),  waited on by push()   */

    int shutdown;              /* set to 1 to unblock all waiters and stop  */
} circular_buffer_t;

/* Initialize / destroy. Must be called once before any other cb_* call. */
void cb_init(circular_buffer_t *cb);
void cb_destroy(circular_buffer_t *cb);

/*
 * Blocking push: copies `len` bytes from `msg` (a malloc+memcpy internally),
 * waits on the condition variable if the buffer is full.
 * Returns 0 on success, -1 if the buffer has been shut down.
 */
int cb_push(circular_buffer_t *cb, const char *msg, size_t len);

/*
 * Blocking pop: waits if the buffer is empty. On success, *out_msg receives
 * ownership of a heap pointer that the CALLER MUST free() after use, and
 * *out_len receives its length.
 * Returns 0 on success, -1 if the buffer has been shut down and is empty.
 */
int cb_pop(circular_buffer_t *cb, char **out_msg, size_t *out_len);

/* Thread-safe snapshot of current occupancy, 0-100 (percent, integer). */
int cb_occupancy_pct(circular_buffer_t *cb);

/* Unblocks any thread currently waiting in cb_push/cb_pop. Call during
 * shutdown so the Producer/Consumer threads can exit their loops cleanly. */
void cb_shutdown(circular_buffer_t *cb);

#endif /* CIRCULAR_BUFFER_H */
