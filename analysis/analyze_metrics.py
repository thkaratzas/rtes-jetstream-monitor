#!/usr/bin/env python3
"""
analyze_metrics.py

Post-processing script for the RTES assignment's metrics_log.txt.
Produces the three plots required by the assignment (Section 5.A.2):

  1. jitter_plot.png            - timing jitter of the Monitor thread (ms) vs time
  2. load_buffer_plot.png       - dual-axis: message rate (Hz) & buffer occupancy (%)
  3. cpu_correlation_plot.png   - message rate (Hz) vs CPU usage (%), with trend line

Usage:
    python3 analyze_metrics.py [path/to/metrics_log.txt]

If no path is given, it looks for "metrics_log.txt" in the current directory.
Plots are saved as PNG files in the same directory the script is run from.
"""

import sys
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use("Agg")  # safe for headless environments (e.g. running on the Pi itself)
import matplotlib.pyplot as plt

COLUMNS = [
    "Seconds", "Nanoseconds", "Commit_Count", "Identity_Count",
    "Account_Count", "Info_Count", "Buffer_Occupancy_Pct", "CPU_Pct",
]


def load_data(path):
    df = pd.read_csv(path, header=None, names=COLUMNS)
    df["Datetime"] = pd.to_datetime(df["Seconds"], unit="s", utc=True)
    df["Jitter_ms"] = df["Nanoseconds"] / 1e6
    df["Msg_Rate_Hz"] = (
        df["Commit_Count"] + df["Identity_Count"]
        + df["Account_Count"] + df["Info_Count"]
    )
    return df


def check_gaps(df):
    """Flags any place where consecutive Seconds values are not exactly
    1 apart - e.g. leftover data from multiple runs appended to the same
    file, or (much less likely) a Monitor deadline missed by >1s."""
    diffs = df["Seconds"].diff().dropna()
    gaps = diffs[diffs != 1]
    if len(gaps) > 0:
        print(f"\n⚠ WARNING: found {len(gaps)} discontinuity/discontinuities "
              f"in the Seconds column (gap != 1s). This usually means multiple "
              f"runs got appended into the same metrics_log.txt. Consider "
              f"splitting/filtering the file to a single continuous run before "
              f"using it for the official report plots.")
        for idx in gaps.index[:10]:
            row = df.loc[idx]
            prev_row = df.loc[idx - 1]
            print(f"   {prev_row['Datetime']}  ->  {row['Datetime']}  "
                  f"(gap = {row['Seconds'] - prev_row['Seconds']:.0f}s)")
        if len(gaps) > 10:
            print(f"   ... and {len(gaps) - 10} more")
    else:
        print("\n✓ No gaps found: Seconds column increases by exactly 1 throughout "
              "(continuous, drift-free recording).")
    return gaps


def plot_jitter(df, outpath="jitter_plot.png"):
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(df["Datetime"], df["Jitter_ms"], linewidth=0.5, color="#d62728")
    ax.axhline(0, color="black", linewidth=0.6)
    ax.set_xlabel("Χρόνος")
    ax.set_ylabel("Jitter (ms)")
    ax.set_title("Jitter Περιοδικού Νήματος (Monitor) ως προς το Ιδανικό Δευτερόλεπτο")

    mean_j, max_j, std_j = df["Jitter_ms"].mean(), df["Jitter_ms"].max(), df["Jitter_ms"].std()
    ax.text(0.01, 0.97,
            f"Mean: {mean_j:.3f} ms   Std: {std_j:.3f} ms   Max: {max_j:.3f} ms",
            transform=ax.transAxes, va="top", fontsize=9,
            bbox=dict(boxstyle="round", facecolor="white", alpha=0.8))

    fig.autofmt_xdate()
    fig.tight_layout()
    fig.savefig(outpath, dpi=150)
    plt.close(fig)
    print(f"Saved {outpath}")


def plot_load_buffer(df, outpath="load_buffer_plot.png"):
    fig, ax1 = plt.subplots(figsize=(12, 5))

    ax1.plot(df["Datetime"], df["Msg_Rate_Hz"], color="#1f77b4", linewidth=0.7)
    ax1.set_xlabel("Χρόνος")
    ax1.set_ylabel("Ρυθμός Μηνυμάτων (Hz)", color="#1f77b4")
    ax1.tick_params(axis="y", labelcolor="#1f77b4")

    ax2 = ax1.twinx()
    ax2.plot(df["Datetime"], df["Buffer_Occupancy_Pct"], color="#ff7f0e", linewidth=0.7)
    ax2.set_ylabel("Πληρότητα Κυκλικού Buffer (%)", color="#ff7f0e")
    ax2.tick_params(axis="y", labelcolor="#ff7f0e")

    ax1.set_title("Φόρτος Εισερχόμενων Μηνυμάτων & Πληρότητα Buffer")
    fig.autofmt_xdate()
    fig.tight_layout()
    fig.savefig(outpath, dpi=150)
    plt.close(fig)
    print(f"Saved {outpath}")


def plot_cpu_correlation(df, outpath="cpu_correlation_plot.png"):
    fig, ax = plt.subplots(figsize=(7, 6))
    x = df["Msg_Rate_Hz"].to_numpy(dtype=float)
    y = df["CPU_Pct"].to_numpy(dtype=float)

    ax.scatter(x, y, s=8, alpha=0.3, color="#2ca02c")

    if len(x) > 1 and np.std(x) > 0:
        coeffs = np.polyfit(x, y, 1)
        trend_x = np.linspace(x.min(), x.max(), 100)
        trend_y = np.polyval(coeffs, trend_x)
        ax.plot(trend_x, trend_y, color="black", linewidth=1.5, linestyle="--")
        corr = np.corrcoef(x, y)[0, 1]
        ax.text(0.02, 0.96, f"Pearson r = {corr:.3f}",
                transform=ax.transAxes, va="top", fontsize=10,
                bbox=dict(boxstyle="round", facecolor="white", alpha=0.8))

    ax.set_xlabel("Ρυθμός Εισερχόμενων Μηνυμάτων (Hz)")
    ax.set_ylabel("Χρήση CPU (%)")
    ax.set_title("Συσχέτιση Ρυθμού Μηνυμάτων & Χρήσης CPU")
    fig.tight_layout()
    fig.savefig(outpath, dpi=150)
    plt.close(fig)
    print(f"Saved {outpath}")


def print_summary(df):
    print("\n--- Στατιστικά για την αναφορά ---")
    print(f"Διάρκεια: {df['Datetime'].iloc[0]}  ->  {df['Datetime'].iloc[-1]}  "
          f"({len(df)} δείγματα, {len(df) / 3600:.2f} ώρες)")
    print(f"Jitter (ms):     mean={df['Jitter_ms'].mean():.3f}  "
          f"std={df['Jitter_ms'].std():.3f}  max={df['Jitter_ms'].max():.3f}")
    print(f"Ρυθμός (Hz):     mean={df['Msg_Rate_Hz'].mean():.1f}  "
          f"min={df['Msg_Rate_Hz'].min()}  max={df['Msg_Rate_Hz'].max()}")
    print(f"Buffer (%):      mean={df['Buffer_Occupancy_Pct'].mean():.2f}  "
          f"max={df['Buffer_Occupancy_Pct'].max()}")
    print(f"CPU (%):         mean={df['CPU_Pct'].mean():.2f}  "
          f"max={df['CPU_Pct'].max()}")


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "metrics_log.txt"
    try:
        df = load_data(path)
    except FileNotFoundError:
        print(f"Δεν βρέθηκε το αρχείο: {path}")
        sys.exit(1)

    print(f"Φορτώθηκαν {len(df)} γραμμές από {path}")
    check_gaps(df)

    plot_jitter(df)
    plot_load_buffer(df)
    plot_cpu_correlation(df)

    print_summary(df)


if __name__ == "__main__":
    main()
