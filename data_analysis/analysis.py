"""
Memory Allocator Statistics Visualizer
Generates all four required metrics for each allocation strategy,
plus cross-strategy comparison charts.

Required metrics:
  1. Average % memory utilization during the program run
  2. % memory utilization as a function of time (per request)
  3. Speed of t_malloc() / t_free() vs allocation size (1 B → 8 MiB, log scale)
  4. Actual data-structure overhead over a program run

Usage:
    python3 analyze.py
    (expects CSV files in the current directory produced by test_main.c)
"""

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
import os, sys

# ── CSV column names (must match log_state() output) ──
COLS = ["type", "size", "time_ns", "allocated", "system", "metadata"]

STRATEGIES = ["first_fit", "best_fit", "worst_fit", "buddy_fit", "mixed_fit"]
LABELS     = {"first_fit": "First Fit", "best_fit": "Best Fit", "worst_fit": "Worst Fit", "buddy_fit": "Buddy Fit", "mixed_fit": "Mixed Fit"}
COLORS     = {"first_fit": "#1f77b4", "best_fit": "#2ca02c", "worst_fit": "#d62728", "buddy_fit": "#ff7f0e", "mixed_fit": "#9467bd"}


def load(path):
    """Load a CSV; return None if missing."""
    if not os.path.exists(path):
        print(f"  [skip] {path} not found")
        return None
    df = pd.read_csv(path, header=None, skipinitialspace=True, names=COLS)
    df["type"] = df["type"].str.strip()
    # Avoid division by zero
    df["utilization"] = np.where(df["system"] > 0, df["allocated"] / df["system"] * 100, 0)
    df["overhead_pct"] = np.where(df["system"] > 0, df["metadata"] / df["system"] * 100, 0)
    return df


def fmt_bytes(x, _):
    """Tick formatter: 1, 1K, 1M, etc."""
    if x >= 1_000_000:
        return f"{x/1_000_000:.0f}M"
    elif x >= 1_000:
        return f"{x/1_000:.0f}K"
    return f"{int(x)}"


# ═══════════════════════════════════════════════════════════════════════
#  Per-strategy figures  (one figure with 4 subplots per strategy)
# ═══════════════════════════════════════════════════════════════════════
def plot_strategy(strategy):
    label = LABELS[strategy]
    color = COLORS[strategy]

    df_speed    = load(f"{strategy}_speed.csv")
    df_workload = load(f"{strategy}_workload.csv")
    df_frag     = load(f"{strategy}_frag.csv")

    # Combine workload + fragmentation data for utilization / overhead metrics
    frames = [d for d in [df_workload, df_frag] if d is not None]
    if not frames:
        print(f"  [skip] No workload/frag data for {strategy}")
        return
    df_combined = pd.concat(frames, ignore_index=True)

    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle(f"Memory Allocator Statistics — {label}", fontsize=15, fontweight="bold")

    # ── 1. % Memory Utilization Over Time (metric 2) ──────────────────
    ax = axes[0, 0]
    util = df_combined["utilization"]
    ax.plot(df_combined.index, util, linewidth=0.4, color=color)
    avg = util.mean()
    ax.axhline(y=avg, color="red", linestyle="--", linewidth=1,
               label=f"Average: {avg:.2f}%")
    ax.set_title("Memory Utilization Over Time")
    ax.set_xlabel("Request #")
    ax.set_ylabel("Utilization (%)")
    ax.set_ylim(0, 105)
    ax.legend(fontsize=9)

    # ── 2. Utilization vs Allocation Size (metric 1 breakdown) ────────
    ax = axes[0, 1]
    df_m = df_combined[df_combined["type"] == "malloc"]
    ax.scatter(df_m["size"], df_m["utilization"], s=2, alpha=0.5, color=color)
    ax.set_title("Utilization vs Allocation Size")
    ax.set_xlabel("Allocation Size (bytes)")
    ax.set_ylabel("Utilization (%)")
    ax.set_xscale("log")
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(fmt_bytes))
    ax.set_ylim(0, 105)

    # ── 3. Speed of t_malloc / t_free vs Size (metric 3) ─────────────
    ax = axes[1, 0]
    if df_speed is not None:
        dm = df_speed[df_speed["type"] == "malloc"]
        df_f = df_speed[df_speed["type"] == "free"]
        ax.scatter(dm["size"], dm["time_ns"], s=6, alpha=0.6,
                   label="t_malloc", color=color)
        ax.scatter(df_f["size"], df_f["time_ns"], s=6, alpha=0.6,
                   label="t_free", color="coral")
    ax.set_title("Allocation / Free Speed vs Size")
    ax.set_xlabel("Size (bytes)")
    ax.set_ylabel("Time (ns)")
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(fmt_bytes))
    ax.legend(markerscale=2, fontsize=9)

    # ── 4. Metadata Overhead Over Time (metric 4) ────────────────────
    ax = axes[1, 1]
    ax.plot(df_combined.index, df_combined["metadata"],
            linewidth=0.4, color=color, label="Metadata (bytes)")
    ax.set_title("Data Structure Overhead Over Time")
    ax.set_xlabel("Request #")
    ax.set_ylabel("Metadata Size (bytes)")
    ax2 = ax.twinx()
    ax2.plot(df_combined.index, df_combined["overhead_pct"],
             linewidth=0.4, color="gray", alpha=0.6, label="% of system")
    ax2.set_ylabel("Overhead (% of system)")
    ax.legend(loc="upper left", fontsize=9)
    ax2.legend(loc="upper right", fontsize=9)

    plt.tight_layout()
    outname = f"{strategy}_stats.png"
    plt.savefig(outname, dpi=200)
    plt.close()

    print(f"  → Saved {outname}")
    print(f"    Average utilization : {avg:.2f}%")
    print(f"    Peak utilization    : {util.max():.2f}%")
    print(f"    Final metadata bytes: {df_combined['metadata'].iloc[-1]}")
    print(f"    Data points         : {len(df_combined)}")


# ═══════════════════════════════════════════════════════════════════════
#  Cross-strategy comparison figure
# ═══════════════════════════════════════════════════════════════════════
def plot_comparison():
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    fig.suptitle("Strategy Comparison", fontsize=15, fontweight="bold")

    avg_utils = {}

    for strat in STRATEGIES:
        color = COLORS[strat]
        label = LABELS[strat]
        df_speed    = load(f"{strat}_speed.csv")
        df_workload = load(f"{strat}_workload.csv")
        df_frag     = load(f"{strat}_frag.csv")

        frames = [d for d in [df_workload, df_frag] if d is not None]
        if not frames:
            continue
        df_c = pd.concat(frames, ignore_index=True)

        avg_utils[label] = df_c["utilization"].mean()

        # ── Utilization over time ──
        axes[0, 0].plot(df_c.index, df_c["utilization"],
                        linewidth=0.3, color=color, alpha=0.7, label=label)

        # ── Speed (malloc only) ──
        if df_speed is not None:
            dm = df_speed[df_speed["type"] == "malloc"]
            axes[1, 0].scatter(dm["size"], dm["time_ns"], s=6, alpha=0.5,
                               color=color, label=label)

        # ── Metadata overhead ──
        axes[1, 1].plot(df_c.index, df_c["metadata"],
                        linewidth=0.4, color=color, alpha=0.7, label=label)

    # ── Bar chart of average utilization ──
    ax = axes[0, 1]
    if avg_utils:
        names = list(avg_utils.keys())
        vals  = list(avg_utils.values())
        bars  = ax.bar(names, vals,
                       color=[COLORS[s] for s in STRATEGIES if LABELS[s] in names])
        ax.set_ylabel("Average Utilization (%)")
        ax.set_title("Avg Memory Utilization (Metric 1)")
        ax.set_ylim(0, 100)
        for bar, v in zip(bars, vals):
            ax.text(bar.get_x() + bar.get_width()/2, v + 1, f"{v:.1f}%",
                    ha="center", fontsize=10)

    axes[0, 0].set_title("Utilization Over Time")
    axes[0, 0].set_xlabel("Request #")
    axes[0, 0].set_ylabel("Utilization (%)")
    axes[0, 0].set_ylim(0, 105)
    axes[0, 0].legend(fontsize=9)

    axes[1, 0].set_title("t_malloc Speed vs Size")
    axes[1, 0].set_xlabel("Size (bytes)")
    axes[1, 0].set_ylabel("Time (ns)")
    axes[1, 0].set_xscale("log")
    axes[1, 0].set_yscale("log")
    axes[1, 0].xaxis.set_major_formatter(ticker.FuncFormatter(fmt_bytes))
    axes[1, 0].legend(markerscale=2, fontsize=9)

    axes[1, 1].set_title("Metadata Overhead Over Time")
    axes[1, 1].set_xlabel("Request #")
    axes[1, 1].set_ylabel("Metadata (bytes)")
    axes[1, 1].legend(fontsize=9)

    plt.tight_layout()
    plt.savefig("comparison_stats.png", dpi=200)
    plt.close()
    print("  → Saved comparison_stats.png")


# ═══════════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    print("=== Per-Strategy Analysis ===")
    for s in STRATEGIES:
        print(f"\n[{LABELS[s]}]")
        plot_strategy(s)

    print("\n=== Cross-Strategy Comparison ===")
    plot_comparison()

    print("\nDone.")