#include "health_monitor.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>

#define HEALTH_LOG_PATH "system_health_log.txt"
#define HEALTH_SAMPLE_PERIOD_SEC 5

static int run_vcgencmd(const char *args, char *out, size_t out_size)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "vcgencmd %s 2>/dev/null", args);

    FILE *p = popen(cmd, "r");
    if (!p) {
        return -1;
    }
    int got = (fgets(out, out_size, p) != NULL);
    pclose(p);
    return got ? 0 : -1;
}

static double read_temp_c(void)
{
    char line[64];
    if (run_vcgencmd("measure_temp", line, sizeof(line)) != 0) {
        return -1.0;
    }
    double t;
    if (sscanf(line, "temp=%lf", &t) == 1) {
        return t;
    }
    return -1.0;
}

static long read_throttled_hex(void)
{
    char line[64];
    if (run_vcgencmd("get_throttled", line, sizeof(line)) != 0) {
        return -1;
    }
    unsigned long v;
    if (sscanf(line, "throttled=0x%lx", &v) == 1) {
        return (long)v;
    }
    return -1;
}

static double read_cpu_clock_mhz(void)
{
    char line[64];
    if (run_vcgencmd("measure_clock arm", line, sizeof(line)) != 0) {
        return -1.0;
    }
    long hz;
    if (sscanf(line, "frequency(%*d)=%ld", &hz) == 1) {
        return (double)hz / 1e6;
    }
    return -1.0;
}

static double read_power_watts(void)
{
    FILE *p = popen("vcgencmd pmic_read_adc 2>/dev/null", "r");
    if (!p) {
        return -1.0;
    }

    typedef struct {
        char base[32];
        double current;
        double voltage;
        int have_current;
        int have_voltage;
    } rail_t;

    rail_t rails[32];
    int n_rails = 0;
    char line[128];

    while (fgets(line, sizeof(line), p)) {
        char label[32];
        int idx;
        double val;
        int is_current = 0, is_voltage = 0;

        if (sscanf(line, "%31s current(%d)=%lf", label, &idx, &val) == 3) {
            is_current = 1;
        } else if (sscanf(line, "%31s volt(%d)=%lf", label, &idx, &val) == 3) {
            is_voltage = 1;
        } else {
            continue;
        }
        (void)idx;

        size_t len = strlen(label);
        if (len > 2 && label[len - 2] == '_') {
            label[len - 2] = '\0';
        }

        int found = -1;
        for (int i = 0; i < n_rails; i++) {
            if (strcmp(rails[i].base, label) == 0) {
                found = i;
                break;
            }
        }
        if (found < 0 && n_rails < 32) {
            found = n_rails++;
            snprintf(rails[found].base, sizeof(rails[found].base), "%s", label);
            rails[found].have_current = 0;
            rails[found].have_voltage = 0;
        }
        if (found >= 0) {
            if (is_current) {
                rails[found].current = val;
                rails[found].have_current = 1;
            } else if (is_voltage) {
                rails[found].voltage = val;
                rails[found].have_voltage = 1;
            }
        }
    }
    pclose(p);

    double total_watts = 0.0;
    for (int i = 0; i < n_rails; i++) {
        if (rails[i].have_current && rails[i].have_voltage) {
            total_watts += rails[i].current * rails[i].voltage;
        }
    }
    return total_watts;
}

static long read_rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) {
        return -1;
    }
    char line[256];
    long rss = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%ld", &rss);
            break;
        }
    }
    fclose(f);
    return rss;
}

void *health_monitor_thread(void *arg)
{
    (void)arg;

    FILE *log = fopen(HEALTH_LOG_PATH, "a");
    if (!log) {
        fprintf(stderr, "[Health] Could not open %s: %s\n", HEALTH_LOG_PATH, strerror(errno));
        return NULL;
    }
    setvbuf(log, NULL, _IOLBF, 0);

    fseek(log, 0, SEEK_END);
    if (ftell(log) == 0) {
        fprintf(log, "Seconds,Nanoseconds,Temp_C,Power_W,Throttled_Hex,RSS_KB,CPU_MHz,Reconnect_Count\n");
        fflush(log);
    }

    while (g_running) {
        for (int i = 0; i < HEALTH_SAMPLE_PERIOD_SEC && g_running; i++) {
            sleep(1);
        }
        if (!g_running) {
            break;
        }

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);

        double temp       = read_temp_c();
        double power       = read_power_watts();
        long   throttled   = read_throttled_hex();
        long   rss_kb       = read_rss_kb();
        double clock_mhz   = read_cpu_clock_mhz();
        long   reconnects  = atomic_load(&g_reconnect_count);

        char throttled_str[24];
        if (throttled >= 0) {
            snprintf(throttled_str, sizeof(throttled_str), "0x%lx", (unsigned long)throttled);
        } else {
            snprintf(throttled_str, sizeof(throttled_str), "NA");
        }

        fprintf(log, "%ld,%ld,%.1f,%.2f,%s,%ld,%.0f,%ld\n",
                (long)now.tv_sec, (long)now.tv_nsec,
                temp, power, throttled_str, rss_kb, clock_mhz, reconnects);
        fflush(log);
    }

    fclose(log);
    return NULL;
}
