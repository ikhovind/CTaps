#!/usr/bin/env python3
"""
Visualize CTaps benchmark results.
Produces two figures:
  1. Box plots of small file transfer time per implementation at each RTT
  2. Median small file transfer time vs RTT (line chart)
"""

import json
import sys
import argparse
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

# Display name and color per test_name
SERIES = {
    "tcp_native":       ("TCP (native)",       "#2196F3"),
    "quic_native":      ("QUIC (native)",       "#4CAF50"),
    "taps_tcp":         ("TAPS/TCP",            "#64B5F6"),
    "taps_quic":        ("TAPS/QUIC",           "#81C784"),
    "taps_racing_tcp":  ("TAPS Racing (→TCP)",  "#FF9800"),
    "taps_racing_quic": ("TAPS Racing (→QUIC)", "#9C27B0"),
}
ORDER = list(SERIES.keys())


def load(path: str) -> dict:
    with open(path) as f:
        return json.load(f)


def collect(data: dict) -> dict:
    """
    Returns:
      result[rtt_ms][test_name] = list of small_file transfer_time_ms values
    """
    result = {}
    for rtt_entry in data["rtt_sweep"]:
        rtt = rtt_entry["rtt_ms"]
        result[rtt] = defaultdict(list)
        for test in rtt_entry["tests"]:
            result[rtt][test["test_name"]].append(
                test["small_file"]["transfer_time_ms"]
            )
    return result


def plot_boxplots(collected: dict, out_dir: Path) -> None:
    rtt_values = sorted(collected.keys())
    ncols = len(rtt_values)
    fig, axes = plt.subplots(1, ncols, figsize=(4 * ncols, 5), sharey=False)
    if ncols == 1:
        axes = [axes]

    for ax, rtt in zip(axes, rtt_values):
        data_per_impl = [collected[rtt].get(name, []) for name in ORDER]
        colors = [SERIES[name][1] for name in ORDER]
        labels = [SERIES[name][0] for name in ORDER]

        bp = ax.boxplot(
            data_per_impl,
            patch_artist=True,
            medianprops=dict(color="black", linewidth=2),
            whiskerprops=dict(linewidth=1.2),
            capprops=dict(linewidth=1.2),
            flierprops=dict(marker="o", markersize=4, alpha=0.5),
        )
        for patch, color in zip(bp["boxes"], colors):
            patch.set_facecolor(color)
            patch.set_alpha(0.8)

        ax.set_title(f"RTT = {rtt} ms", fontsize=11)
        ax.set_xticks(range(1, len(ORDER) + 1))
        ax.set_xticklabels(labels, rotation=30, ha="right", fontsize=8)
        ax.set_ylabel("Small file transfer time (ms)" if ax == axes[0] else "")
        ax.yaxis.set_minor_locator(ticker.AutoMinorLocator())
        ax.grid(axis="y", linestyle="--", alpha=0.5)

    fig.suptitle("Small file transfer time by implementation and RTT", fontsize=13, y=1.02)
    fig.tight_layout()
    out = out_dir / "boxplots.png"
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved {out}")
    plt.close(fig)


def plot_rtt_sweep(collected: dict, out_dir: Path) -> None:
    rtt_values = sorted(collected.keys())

    fig, ax = plt.subplots(figsize=(8, 5))

    for name in ORDER:
        label, color = SERIES[name]
        medians = [np.median(collected[rtt].get(name, [np.nan])) for rtt in rtt_values]
        p25 = [np.percentile(collected[rtt].get(name, [np.nan]), 25) for rtt in rtt_values]
        p75 = [np.percentile(collected[rtt].get(name, [np.nan]), 75) for rtt in rtt_values]

        ax.plot(rtt_values, medians, marker="o", label=label, color=color, linewidth=2)
        ax.fill_between(rtt_values, p25, p75, alpha=0.15, color=color)

    ax.set_xlabel("RTT (ms)", fontsize=11)
    ax.set_ylabel("Small file transfer time (ms)", fontsize=11)
    ax.set_title("Small file transfer time vs RTT (median ± IQR)", fontsize=12)
    ax.set_xticks(rtt_values)
    ax.legend(fontsize=9)
    ax.grid(linestyle="--", alpha=0.5)
    fig.tight_layout()

    out = out_dir / "rtt_sweep.png"
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved {out}")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description="Plot CTaps benchmark results")
    parser.add_argument("input", help="Path to benchmark JSON file")
    parser.add_argument("--out-dir", default=".", help="Output directory for plots")
    args = parser.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    data = load(args.input)
    collected = collect(data)

    plot_boxplots(collected, out_dir)
    plot_rtt_sweep(collected, out_dir)


if __name__ == "__main__":
    main()
