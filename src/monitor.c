#include "monitor.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define LOG_FILE_PATH "metrics_log.txt"

typedef struct {
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
} cpu_stat_t;

static int read_cpu_stat(cpu_stat_t *out)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) {
        return -1;
    }
    char line[256];
    int ok = (fgets(line, sizeof(line), f) != NULL);
    fclose(f);
    if (!ok) {
        return -1;
    }

    unsigned long long user = 0, nice = 0, sys = 0, idle = 0,
                        iowait = 0, irq = 0, softirq = 0, steal = 0;
    int n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                    &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal);
    if (n < 4) {
        return -1;
    }

    out->user = user; out->nice = nice; out->system = sys; out->idle = idle;
    out->iowait = iowait; out->irq = irq; out->softirq = softirq; out->steal = steal;
    return 0;
}

static double cpu_pct_between(const cpu_stat_t *prev, const cpu_stat_t *cur)
{
    unsigned long long prev_idle = prev->idle + prev->iowait;
    unsigned long long cur_idle  = cur->idle  + cur->iowait;

    unsigned long long prev_busy = prev->user + prev->nice + prev->system +
                                    prev->irq + prev->softirq + prev->steal;
    unsigned long long cur_busy  = cur->user  + cur->nice  + cur->system +
                                    cur->irq  + cur->softirq  + cur->steal;

    unsigned long long prev_total = prev_idle + prev_busy;
    unsigned long long cur_total  = cur_idle  + cur_busy;

    if (cur_total <= prev_total) {
        return 0.0;
    }

    unsigned long long delta_total = cur_total - prev_total;
    unsigned long long delta_idle  = cur_idle  - prev_idle;

    return (double)(delta_total - delta_idle) * 100.0 / (double)delta_total;
}

void *monitor_thread(void *arg)
{
    (void)arg;

    FILE *log = fopen(LOG_FILE_PATH, "a");
    if (!log) {
        fprintf(stderr, "[Monitor] Could not open %s: %s\n", LOG_FILE_PATH, strerror(errno));
        return NULL;
    }
    setvbuf(log, NULL, _IOLBF, 0);

    cpu_stat_t prev_cpu;
    int have_prev_cpu = (read_cpu_stat(&prev_cpu) == 0);

    struct timespec next;
    clock_gettime(CLOCK_REALTIME, &next);
    next.tv_sec += 1;
    next.tv_nsec = 0;

    while (g_running) {
        int rc;
        do {
            rc = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &next, NULL);
        } while (rc == EINTR && g_running);

        if (rc != 0 && rc != EINTR) {
            fprintf(stderr, "[Monitor] clock_nanosleep error: %s\n", strerror(rc));
            break;
        }
        if (!g_running) {
            break;
        }

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);

        pthread_mutex_lock(&g_counters.mutex);
        long commit_c   = g_counters.commit_count;
        long identity_c = g_counters.identity_count;
        long account_c  = g_counters.account_count;
        long info_c     = g_counters.info_count;
        g_counters.commit_count   = 0;
        g_counters.identity_count = 0;
        g_counters.account_count  = 0;
        g_counters.info_count     = 0;
        pthread_mutex_unlock(&g_counters.mutex);

        int buf_pct = cb_occupancy_pct(&g_cb);

        cpu_stat_t cur_cpu;
        double cpu_pct = 0.0;
        if (read_cpu_stat(&cur_cpu) == 0) {
            if (have_prev_cpu) {
                cpu_pct = cpu_pct_between(&prev_cpu, &cur_cpu);
            }
            prev_cpu = cur_cpu;
            have_prev_cpu = 1;
        }

        fprintf(log, "%ld,%ld,%ld,%ld,%ld,%ld,%d,%.2f\n",
                (long)now.tv_sec, (long)now.tv_nsec,
                commit_c, identity_c, account_c, info_c,
                buf_pct, cpu_pct);
        fflush(log);

        next.tv_sec += 1;
    }

    fclose(log);
    return NULL;
}
