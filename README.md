# RTES Jetstream Monitor

A multi-threaded, real-time embedded system in C that connects to the [Bluesky Jetstream](https://docs.bsky.app/docs/advanced-guides/firehose) WebSocket firehose, asynchronously ingests live AT Protocol events, and produces drift-free periodic telemetry logs, built and validated on a **Raspberry Pi 5** running Raspberry Pi OS (Debian 12 "Bookworm").

Developed as coursework for a Real-Time Embedded Systems assignment: implement a Producer-Consumer pipeline with POSIX threads, bounded circular buffering, and a strictly periodic logger free of clock drift, then run it unattended for a full 24-hour data collection window.

## Overview

The program connects to:wss://jetstream1.us-east.bsky.network/subscribe?wantedCollections=app.bsky.feed.postand classifies every incoming JSON event by its `"kind"` field (`commit`, `identity`, `account`, `info`), while a strictly periodic thread logs throughput, buffer occupancy, and CPU usage once per second — with no cumulative timing drift, even over a full 24-hour unattended run.

## Architecture

Four POSIX threads share a bounded circular buffer and a mutex-protected counter set:

| Thread | Role | Key design points |
|---|---|---|
| **Producer** | Connects to the Jetstream WebSocket (via `libwebsockets`), reassembles fragmented frames, pushes complete JSON text onto the circular buffer | Event-driven (`lws_service`), never blocks on the network; automatic reconnect with exponential backoff (1s → 30s cap), backoff resets after any genuine established connection |
| **Consumer** | Pops JSON text, parses it with `cJSON`, increments the matching message-kind counter | No I/O in the hot path (no `printf`/file writes) per the assignment's performance requirement; malformed/unknown JSON is silently dropped, never crashes the thread |
| **Monitor** | Wakes exactly once per second, aligned to the wall-clock second boundary; snapshots + resets the four counters, samples buffer occupancy and CPU usage, appends one CSV row | Uses `clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, ...)` with the *absolute* target time incremented by exactly 1s each iteration — this is what keeps the loop drift-free over 24h, since jitter in one wakeup never carries over to the next deadline |
| **Health Monitor** *(supplementary, not required by the assignment)* | Every 5s, logs CPU temperature, estimated SoC power draw, throttling status, process RSS memory, CPU clock speed, and the Producer's reconnect count to a **separate** file | Kept fully isolated from the required `metrics_log.txt` format/timing so it can never interfere with the graded real-time behavior |

### Synchronization & the circular buffer

- Bounded circular queue (`circular_buffer.c`), capacity 2048 slots, storing heap-allocated `(char*, length)` pairs rather than fixed-size arrays — Jetstream messages vary substantially in size (a bare `commit` vs. one with embeds/reply chains/facets), so fixed-size slots would waste large amounts of memory for the worst case × 2048.
- Classic `pthread_mutex_t` + two `pthread_cond_t` (`not_empty`, `not_full`) implementation of the bounded-buffer pattern; a `shutdown` flag lets any thread currently blocked in `cb_push`/`cb_pop` wake up and exit cleanly during shutdown instead of hanging forever.
- The `malloc`+`memcpy` for each pushed message happens **before** the mutex is acquired, keeping the critical section as short as possible.
- Race-condition handling verified with `valgrind --tool=helgrind` (0 errors) in addition to `--tool=memcheck` (0 leaks, 0 errors), both on synthetic multi-producer/multi-consumer stress tests and on the full live system running against real Jetstream traffic.

### Clean shutdown

`SIGINT`/`SIGTERM` are handled by a minimal signal handler that only flips a `volatile sig_atomic_t` flag (the only async-signal-safe option here, since `pthread_mutex_lock` is not guaranteed safe to call from a signal handler). The main thread then calls `cb_shutdown()` to unblock any thread waiting on the buffer, and joins all four threads in sequence.

## Repository structure.
├── Makefile
├── include/              # Header files (5 module headers + common.h shared state)
├── src/                  # Implementation (main.c + 5 modules)
├── systemd/
│   └── rtes-monitor.service   # Unit file used to run the 24h collection unattended
├── data/
│   ├── metrics_log.txt        # Required deliverable: 24h, 1 Hz, CSV
│   └── system_health_log.txt  # Supplementary: temperature/power/RSS/reconnects, 0.2 Hz
└── analysis/
├── analyze_metrics.py     # Post-processing: generates the 3 report plots + summary stats
├── jitter_plot.png
├── load_buffer_plot.png
└── cpu_correlation_plot.png## Build

Requires `libwebsockets-dev` and `libcjson-dev` (available via `apt` on Raspberry Pi OS / Debian):

```bash
sudo apt install -y build-essential libwebsockets-dev libssl-dev libcjson-dev
make
```

This produces the `rtes_monitor` binary.

## Run

**Foreground (manual testing):**
```bash
./rtes_monitor
```
Logs to `metrics_log.txt` and `system_health_log.txt` in the working directory. Stop with `Ctrl+C` for a clean, logged shutdown.

**As a systemd service (used for the actual 24h collection run):**
```bash
sudo cp systemd/rtes-monitor.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable rtes-monitor   # survives reboot
sudo systemctl start rtes-monitor
sudo systemctl status rtes-monitor
```
This lets the collection run unattended and survive SSH disconnects, verified in practice by disconnecting and reconnecting over both LAN and [Tailscale](https://tailscale.com) during the actual 24h run.

## Data format

### `metrics_log.txt` (required, exactly as specified by the assignment)

CSV, one row per second, no header:Seconds,Nanoseconds,Commit_Count,Identity_Count,Account_Count,Info_Count,Buffer_Occupancy_Pct,CPU_Pct- `Seconds`/`Nanoseconds` — `CLOCK_REALTIME` timestamp of the logging instant (`clock_gettime`); `Nanoseconds` directly represents timing jitter, since `clock_nanosleep(TIMER_ABSTIME)` can wake late but never early.
- `Commit_Count` … `Info_Count` — messages of each Jetstream `"kind"` received in the preceding 1-second window.
- `Buffer_Occupancy_Pct` — circular buffer fill level (0–100) at the sampling instant.
- `CPU_Pct` — system CPU usage over the preceding second, computed from `/proc/stat` jiffy deltas.

### `system_health_log.txt` (supplementary, not required)Seconds,Nanoseconds,Temp_C,Power_W,Throttled_Hex,RSS_KB,CPU_MHz,Reconnect_CountSampled every 5 seconds via `vcgencmd` and `/proc/self/status`.

## Post-processing / analysis

```bash
python3 -m venv venv && source venv/bin/activate
pip install pandas matplotlib numpy
cd analysis
python3 analyze_metrics.py ../data/metrics_log.txt
```

Produces the three plots required by the assignment (all included in `analysis/`):

1. **`jitter_plot.png`** — Monitor thread's timing offset (ms) from the ideal second boundary, over the full run.
2. **`load_buffer_plot.png`** — dual-axis: incoming message rate (Hz) vs. circular buffer occupancy (%), showing network burstiness.
3. **`cpu_correlation_plot.png`** — scatter plot of message rate (Hz) vs. CPU usage (%), with a linear trend line and Pearson correlation coefficient.

The script also prints summary statistics and automatically flags any discontinuity in the `Seconds` column (e.g. leftover data from multiple appended runs).

## Results — 24-hour collection run

Collected continuously on physical Raspberry Pi 5 hardware (no simulation), **2026-07-11 20:59:32 UTC → 2026-07-12 20:59:33 UTC** (24h 00m 01s, 86,402 samples, zero gaps):

| Metric | Value |
|---|---|
| Timing jitter | mean 0.057 ms · std 0.012 ms · max 3.401 ms |
| Message rate | mean 43.3 Hz · max 412 Hz (burst) |
| Circular buffer occupancy | **0% throughout — never exceeded**, even at peak burst |
| CPU usage | mean 0.16% · max 11.06% |
| CPU temperature (passive cooling, no fan/heatsink) | max 49.9 °C |
| Network reconnects | 0 |
| Memory leaks (`valgrind --leak-check=full`) | 0 bytes leaked, 0 errors |
| Race conditions (`valgrind --tool=helgrind`) | 0 errors |

The circular buffer never exceeded 0% occupancy even during the largest observed burst (412 Hz, ~10× the mean rate), indicating the Consumer thread comfortably outpaces real-world Jetstream traffic with the current buffer sizing (2048 slots).

## Hardware & software

- Raspberry Pi 5, passively cooled (no fan/heatsink attached)
- Raspberry Pi OS (Debian 12 "Bookworm"), 64-bit, running headless (console mode)
- `libwebsockets` 4.1.6, `cJSON` 1.7.15, GCC, POSIX threads

## Author

Athanasios Karatzas, School of Electrical & Computer Engineering (ECE), Electronics and Computer Sector, as part of the course of Embedded Real Time Systems, Supervising professor: Nikolaos Pitsianis, Aristotle University of Thessaloniki (AUTh)
