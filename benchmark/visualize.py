#!/usr/bin/env python3
"""
Visualize CTaps benchmark results.
Produces two figures:
  1. Grouped bar chart: small file transfer time per implementation at each RTT
  2. Split line chart: TCP family vs QUIC family vs RTT
"""

import json
import argparse
from collections import defaultdict
from pathlib import Path
import seaborn as sns

import matplotlib
import matplotlib.pyplot as plt
import numpy as np

SERIES = {
    "tcp_native":       ("TCP (Native)",        "#1565C0", "solid"),
    "taps_racing_tcp":  ("TCP (CTaps)",   "#90CAF9", "dashed"),
    "quic_native":      ("QUIC (Picoquic)",         "#2E7D32", "solid"),
    "taps_racing_quic": ("QUIC (CTaps)",   "#A5D6A7", "dashed"),
    "quic_native_dual_ip":      ("Picoquic",         "#90CAF9", "solid"),
    "taps_racing_quic_dual_ip": ("CTaps",   "#A5D6A7", "dashed"),
}

TCP_FAMILY  = ["tcp_native", "taps_racing_tcp"]
QUIC_FAMILY = ["quic_native", "taps_racing_quic"]
DUAL_IP_QUIC_HANDSHAKE = ["quic_native_dual_ip", "taps_racing_quic_dual_ip"]

FIGSIZE_PT = 395.25368 # Originally 418.25368 but shaved a bit
FIGSIZE_IN = FIGSIZE_PT / 72.27
DEFAULT_FIGSIZE = (FIGSIZE_IN, FIGSIZE_IN / 1.618)

def load(path):
    with open(path) as f:
        return json.load(f)


def collect(data):
    result = {}
    for rtt_entry in data["rtt_sweep"]:
        rtt = rtt_entry["rtt_ms"]
        result[rtt] = defaultdict(list)
        for test in rtt_entry["tests"]:
            result[rtt][test["test_name"]].append(
                test["small_file"]["transfer_time_ms"]
            )
    return result


def plot_grouped_bars(collected, out_dir, use_pgf=True):
    rtt_values = sorted(collected.keys())
    short_file_impls = [key for key in SERIES.keys() if key not in DUAL_IP_QUIC_HANDSHAKE]
    n_impls   = len(short_file_impls)
    n_rtts    = len(rtt_values)
    bar_width = 0.8 / n_impls
    x = np.arange(n_rtts)

    fig, ax = plt.subplots(figsize=DEFAULT_FIGSIZE)

    for i, name in enumerate(short_file_impls):
        label, color, linestyle = SERIES[name]
        samples = [collected[rtt].get(name, [np.nan]) for rtt in rtt_values]

        medians = [np.median(s) for s in samples]

        # Asymmetric error bars: [median - p25, p75 - median] per bar.
        # Collapse to None when a group has only one sample (no spread to show).
        if any(len(s) > 1 for s in samples):
            yerr_lo = [
                np.median(s) - np.percentile(s, 25) if len(s) > 1 else 0.0
                for s in samples
            ]
            yerr_hi = [
                np.percentile(s, 75) - np.median(s) if len(s) > 1 else 0.0
                for s in samples
            ]
            yerr = [yerr_lo, yerr_hi]
        else:
            yerr = None

        offset = (i - n_impls / 2 + 0.5) * bar_width
        ax.bar(
            x + offset, medians, bar_width * 0.9,
            label=label, color=color,
            yerr=yerr,
            capsize=3,
            error_kw=dict(elinewidth=1, ecolor="black", alpha=0.7),
        )

    ax.axvline(x=n_rtts / 2 - 0.5, color="none")
    ax.set_xticks(x)
    ax.set_xticklabels([f"{r} ms" for r in rtt_values])
    ax.set_xlabel("RTT", fontsize=11)
    ax.set_ylabel("Small file transfer time (ms)", fontsize=11)
    ax.set_title("Small file transfer time by implementation and RTT", fontsize=11)
    ax.legend(fontsize=9, ncol=2)
    ax.grid(axis="y", linestyle="--", alpha=0.4)
    fig.tight_layout()

    filename = "bar_chart.pgf" if use_pgf else "bar_chart.png"
    out = out_dir / filename
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved {out}")
    plt.close(fig)


def plot_split_lines(collected, out_dir, use_pgf=True):
    rtt_values = sorted(collected.keys())
    fig, ax = plt.subplots(figsize=DEFAULT_FIGSIZE)

    for family in [TCP_FAMILY, QUIC_FAMILY]:
        for name in family:
            label, color, linestyle = SERIES[name]
            medians = [np.median(collected[rtt].get(name, [np.nan])) for rtt in rtt_values]
            stds    = [np.std(collected[rtt].get(name, [np.nan]))    for rtt in rtt_values]
            ax.plot(rtt_values, medians, marker="o", label=label,
                    linestyle=linestyle,
                    color=color, linewidth=2.5, zorder=3)
            ax.fill_between(
                rtt_values,
                [m - s for m, s in zip(medians, stds)],
                [m + s for m, s in zip(medians, stds)],
                alpha=0.2, color=color
            )

    ax.set_xlabel("RTT (ms)", fontsize=11)
    ax.set_ylabel("Small file transfer time (ms)", fontsize=11)
    ax.set_title(
        "Small file transfer time by RTT",
        fontsize=11
    )
    ax.set_xticks(rtt_values)
    ax.legend(fontsize=9)
    ax.grid(linestyle="--", alpha=0.4)
    fig.tight_layout()
    filename = "rtt_sweep.pgf" if use_pgf else "rtt_sweep.png"
    out = out_dir / filename 
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved {out}")
    plt.close(fig)


def plot_migration_throughput(migration_data: list, out_dir: Path,
                              use_pgf: bool = True,
                              bucket_duration_ms: float = 100.0,
                              path_change_ms: float = 500.0) -> None:
    """
    Plot throughput over time for the migration scenario, averaged across runs.
    Each entry in migration_data is a dict with keys:
      - "label":  display name
      - "color":  hex color
      - "runs":   list of chunk lists, each list being [{"t_ms": float, "bytes": int}, ...]
    """
    fig, ax = plt.subplots(figsize=DEFAULT_FIGSIZE)

    for series in migration_data:
        runs = series["runs"]
        if not runs:
            continue

        # Determine n_buckets from the longest run
        total_ms = max(
            max(c["t_ms"] for c in run) for run in runs if run
        )
        n_buckets = max(1, int(np.ceil(total_ms / bucket_duration_ms)))

        # Accumulate per-run bucket arrays then average
        all_runs = np.zeros((len(runs), n_buckets))
        for r_idx, chunks in enumerate(runs):
            for c in chunks:
                idx = min(int(c["t_ms"] / bucket_duration_ms), n_buckets - 1)
                all_runs[r_idx, idx] += c["bytes"]

        median_bytes = np.median(all_runs, axis=0)
        q25_bytes    = np.percentile(all_runs, 25, axis=0)
        q75_bytes    = np.percentile(all_runs, 75, axis=0)

        times      = [(i + 0.5) * bucket_duration_ms for i in range(n_buckets)]
        throughput = [(b * 8) / (bucket_duration_ms * 1e3) for b in median_bytes]
        q25_mbps   = [(b * 8) / (bucket_duration_ms * 1e3) for b in q25_bytes]
        q75_mbps   = [(b * 8) / (bucket_duration_ms * 1e3) for b in q75_bytes]

        ax.plot(times, throughput,
                marker="o", markersize=4,
                label=series["label"],
                color=series["color"],
                linewidth=2)
        ax.fill_between(
            times,
            q25_mbps,
            q75_mbps,
            alpha=0.15, color=series["color"]
        )

    ax.axvline(x=path_change_ms, color="red", linestyle="--", linewidth=1.5,
               label=f"Path becomes unavailable (T={path_change_ms:.0f}ms)")

    ax.set_xlabel("Time since transfer start (ms)", fontsize=11)
    ax.set_ylabel("Throughput (Mbps)", fontsize=11)
    ax.set_title("QUIC recovery after connection migration\n", fontsize=11)
    ax.set_ylim(bottom=0, top=8)
    ax.legend(fontsize=9)
    ax.grid(linestyle="--", alpha=0.4)

    fig.tight_layout()
    filename = "migration_throughput.pgf" if use_pgf else "migration_throughput.png"
    out = out_dir / filename 
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved {out}")
    plt.close(fig)

def collect_handshake(data):
    """result[rtt_ms][test_name] = list of large_file handshake_time_ms"""
    result = {}
    for rtt_entry in data["rtt_sweep"]:
        rtt = rtt_entry["rtt_ms"]
        result[rtt] = defaultdict(list)
        for test in rtt_entry["tests"]:
            result[rtt][test["test_name"]].append(
                test["large_file"]["handshake_time_ms"]
            )
    return result


def plot_racing_handshake(handshake: dict, out_dir: Path, use_pgf=True) -> None:
    """
    Connection establishment time vs RTT for all implementations.
    The racing stagger penalty (250ms) is visible on taps_racing_tcp,
    while taps_racing_quic tracks taps_quic closely.
    """
    rtt_values = sorted(handshake.keys())

    fig, ax = plt.subplots(figsize=DEFAULT_FIGSIZE)

    for name in TCP_FAMILY + QUIC_FAMILY:
        label, color, linestyle = SERIES[name]
        # Thicker / solid line for racing, thinner dashed for non-racing
        lw      = 2.5 if "racing" in name else 1.5
        ls      = "-"  if "racing" in name else "--"
        marker  = "o"  if "racing" in name else "s"

        medians = [np.median(handshake[rtt].get(name, [np.nan])) for rtt in rtt_values]
        stds    = [np.std(handshake[rtt].get(name, [np.nan]))    for rtt in rtt_values]

        ax.plot(rtt_values, medians, marker=marker, label=label,
                color=color, linewidth=lw, linestyle=linestyle, zorder=3)
        ax.fill_between(
            rtt_values,
            [m - s for m, s in zip(medians, stds)],
            [m + s for m, s in zip(medians, stds)],
            alpha=0.15, color=color
        )

    # Annotate the minimum 250ms stagger offset as a callout
    min_handshake = min(handshake.keys())
    tcp_native_time   = np.median(handshake[min_handshake].get("tcp_native", [np.nan]))
    tcp_racing_time = np.median(handshake[min_handshake].get("taps_racing_tcp", [np.nan]))
    tcp_stagger = ((tcp_racing_time - tcp_native_time) // 250) * 250
    ax.annotate(
        f"+{tcp_stagger:.0f} ms\n(stagger delay)",
        xy=(min_handshake, tcp_racing_time), xytext=(min_handshake + 10, tcp_racing_time + 80),
        arrowprops=dict(arrowstyle="->", color="gray"),
        fontsize=8, color="gray"
    )

    ax.set_xlabel("RTT (ms)", fontsize=11)
    ax.set_ylabel("Handshake time (ms)", fontsize=11)
    ax.set_title(
        "Handshake time vs RTT",
        fontsize=11
    )
    ax.set_xticks(rtt_values)
    ax.legend(fontsize=9)
    ax.grid(linestyle="--", alpha=0.4)

    fig.tight_layout()
    filename = "racing_handshake.pgf" if use_pgf else "racing_handshake.png"
    out = out_dir / filename 
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved {out}")
    plt.close(fig)

def plot_handshake_kde(handshake: dict, out_dir: Path, use_pgf=True) -> None:
    if len(handshake) != 1:
        raise ValueError("Expected handshake data for exactly one RTT value to plot KDE")

    rtt_value = list(handshake.keys())[0]
    fig, ax = plt.subplots(figsize=DEFAULT_FIGSIZE)

    theoretical = {
        "quic_native_dual_ip": 2 * rtt_value,
        "taps_racing_quic_dual_ip":     250 + 2 * 100
    }
    for i, (name, t) in enumerate(theoretical.items()):
        ax.axvline(
            t, color="red", linestyle="--", linewidth=1.2, alpha=0.8,
            label="Theoretical minima" if i == 0 else "_nolegend_"
        )

    for name in DUAL_IP_QUIC_HANDSHAKE:
        label, color, linestyle = SERIES[name]

        rtts = handshake[rtt_value].get(name, [np.nan])

        # KDE requires at least 2 data points to form a distribution
        plot = sns.kdeplot(
            data=rtts,
            ax=ax,
            label=label,
            color=color,
            fill=True,
            alpha=0.4,
            linewidth=1.5,
            warn_singular=False
        )

        plot.set_xlim(300, 900)

    ax.set_xlabel("Handshake Time (ms)")
    ax.set_ylabel("Probability Density")
    ax.set_title(f"Effect of Candidate Racing on Connection Establishment Time")

    ax.legend(fontsize=9, frameon=True, loc="upper left", bbox_to_anchor=(0.62, 0.98))
    
    ax.grid(linestyle="--", alpha=0.4)
    sns.despine()

    fig.tight_layout()
    
    ext = "pgf" if use_pgf else "png"
    out = out_dir / f"handshake_kde_{rtt_value}ms.{ext}"
    
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved {out}")
    plt.close(fig)

def main():
    parser = argparse.ArgumentParser(description="Plot CTaps benchmark results")
    parser.add_argument("input", help="Path to benchmark JSON file")
    parser.add_argument("--output", default=".", help="Output directory for plots")
    parser.add_argument("--migration", help="Whether to plot as migration scenario", action="store_true")
    parser.add_argument("--dual-handshake", help="Whether to plot as dual-handshake scenario", action="store_true")
    parser.add_argument("--small-file", help="Whether to plot small file transfer", action="store_true")
    parser.add_argument("--handshake", help="Whether to plot simple handshake scenario", action="store_true")
    parser.add_argument("--rtt-sweep", help="Whether to plot rtt sweep scenario", action="store_true")
    parser.add_argument("--bucket-duration-ms", type=float, default=100.0,
                        help="Duration of each throughput bucket in ms (default: 100)")
    parser.add_argument("--path-change-ms", type=float, default=500.0,
                        help="When the path change was triggered in ms (default: 500)")
    parser.add_argument("--pgf", help="Use PGF backend for LaTeX-quality plots", action="store_true", default=False)
    args = parser.parse_args()

    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    if not args.pgf:
        matplotlib.use("pgf")
        # Match UiO thesis
        matplotlib.rcParams.update({
            "pgf.texsystem": "pdflatex",
            "font.family": "serif",       # body text uses LM serif
            "text.usetex": True,
            "pgf.rcfonts": False,
        })

    data = load(args.input)

    if args.migration:
        migration_data = []
        runs_by_name = defaultdict(list)
        for rtt_entry in data.get("rtt_sweep", []):
            for test in rtt_entry.get("tests", []):
                name = test.get("test_name")
                if name not in SERIES:
                    continue
                chunks = test.get("large_file", {}).get("buckets")
                if not chunks:
                    continue
                runs_by_name[name].append(chunks)
        for name, runs in runs_by_name.items():
            label, color, linestyle = SERIES[name]
            migration_data.append({
                "label": label,
                "color": color,
                "runs":  runs,
            })
        if migration_data:
            plot_migration_throughput(
                migration_data, out_dir,
                bucket_duration_ms=args.bucket_duration_ms,
                path_change_ms=args.path_change_ms,
                use_pgf=args.pgf
            )
        else:
            print("Warning: no usable migration data found in migration file")
    else:
        collected = collect(data)

        if args.small_file:
            plot_grouped_bars(collected, out_dir, use_pgf=args.pgf)

        if args.rtt_sweep:
            plot_split_lines(collected, out_dir, use_pgf=args.pgf)
        
        if args.handshake:
            handshake = collect_handshake(data)
            plot_racing_handshake(handshake, out_dir, use_pgf=args.pgf)

        if args.dual_handshake:
            plot_handshake_kde(handshake, out_dir, use_pgf=args.pgf)



if __name__ == "__main__":
    main()
