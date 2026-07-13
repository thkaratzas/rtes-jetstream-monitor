#include "common.h"
#include "producer.h"
#include "consumer.h"
#include "monitor.h"
#include "health_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>

circular_buffer_t     g_cb;
message_counters_t    g_counters;
volatile sig_atomic_t g_running = 1;
atomic_long            g_reconnect_count = 0;

static void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static void install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

int main(void)
{
    install_signal_handlers();

    cb_init(&g_cb);
    pthread_mutex_init(&g_counters.mutex, NULL);

    pthread_t prod_tid, cons_tid, mon_tid, health_tid;

    if (pthread_create(&prod_tid, NULL, producer_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create Producer thread\n");
        return 1;
    }
    if (pthread_create(&cons_tid, NULL, consumer_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create Consumer thread\n");
        return 1;
    }
    if (pthread_create(&mon_tid, NULL, monitor_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create Monitor thread\n");
        return 1;
    }
    if (pthread_create(&health_tid, NULL, health_monitor_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create Health Monitor thread\n");
        return 1;
    }

    printf("All threads started. Logging to metrics_log.txt (+ system_health_log.txt). Press Ctrl+C to stop.\n");

    while (g_running) {
        pause();
    }

    printf("\nShutdown signal received, stopping threads...\n");

    cb_shutdown(&g_cb);

    pthread_join(prod_tid, NULL);
    pthread_join(cons_tid, NULL);
    pthread_join(mon_tid, NULL);
    pthread_join(health_tid, NULL);

    cb_destroy(&g_cb);
    pthread_mutex_destroy(&g_counters.mutex);

    printf("Clean shutdown complete.\n");
    return 0;
}
