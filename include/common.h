#ifndef COMMON_H
#define COMMON_H

#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include "circular_buffer.h"

typedef struct {
    long commit_count;
    long identity_count;
    long account_count;
    long info_count;
    pthread_mutex_t mutex;
} message_counters_t;

extern circular_buffer_t   g_cb;
extern message_counters_t  g_counters;

extern volatile sig_atomic_t g_running;

extern atomic_long g_reconnect_count;

#endif /* COMMON_H */
