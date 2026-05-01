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
    "tcp_native":               ("TCP (Native)",    "#1565C0"),
    "taps_racing_tcp":          ("TCP (CTaps)",     "#90CAF9"),
    "quic_native":              ("QUIC (Picoquic)", "#2E7D32"),
    "taps_racing_quic":         ("QUIC (CTaps)",    "#A5D6A7"),
    "quic_native_dual_ip":      ("Picoquic",        "#90CAF9"),
    "taps_racing_quic_dual_ip": ("CTaps (QUIC)",           "#A5D6A7"),
}

DUAL_IP_QUIC_HANDSHAKE = ["quic_native_dual_ip", "taps_racing_quic_dual_ip"]
HANDSHAKE_TEST_NAMES = {
    "tcp_handshake_test":  "tcp_native",
    "quic_handshake_test": "quic_native",
    "taps_handshake_quic": "taps_racing_quic",
    "taps_handshake_tcp":  "taps_racing_tcp",
}

FIGSIZE_PT = 395.25368  # Originally 418.25368 but shaved a bit
FIGSIZE_IN = FIGSIZE_PT / 72.27
DEFAULT_FIGSIZE = (FIGSIZE_IN, FIGSIZE_IN / 1.618)


def configure_style(use_pgf: bool) -> None:
    """Apply global matplotlib styling. Call once from main()."""
    plt.rcParams.update({
        "font.family":     "serif",
        "font.size":       10,
        "axes.labelsize":  11,
        "axes.titlesize":  11,
        "legend.fontsize": 9,
        "axes.grid":       True,
        "grid.linestyle":  "--",
        "grid.alpha":      0.4,
    })
    if use_pgf:
        matplotlib.use("pgf")
        plt.rcParams.update({ "pgf.texsystem": "pdflatex",
            "text.usetex":   True,
            "pgf.rcfonts":   False,
            "pgf.preamble":  r"\usepackage{siunitx}",
        })


def load(path):
    with open(path) as f:
        return json.load(f)


def collect(data):
    result = {}
    for rtt_entry in data["rtt_results"]:
        rtt = rtt_entry["rtt_ms"]
        result[rtt] = defaultdict(list)
        for test in rtt_entry["tests"]:
            result[rtt][test["test_name"]].append(
                test["small_file"]["transfer_time_ms"]
            )
    return result


def plot_small_file(collected, out_dir, use_pgf=True):
    rtt_values = sorted(collected.keys())
    short_file_impls = [key for key in SERIES.keys() if key not in DUAL_IP_QUIC_HANDSHAKE]
    n_impls = len(short_file_impls)
    n_rtts  = len(rtt_values)
    bar_width = 0.8 / n_impls
    x = np.arange(n_rtts)

    fig, ax = plt.subplots(figsize=DEFAULT_FIGSIZE)

    for i, name in enumerate(short_file_impls):
        label, color = SERIES[name]
        samples = [collected[rtt].get(name, [np.nan]) for rtt in rtt_values]
        medians = [np.median(s) for s in samples]

        offset = (i - n_impls / 2 + 0.5) * bar_width
        ax.bar(
            x + offset, medians, bar_width * 0.9,
            label=label, color=color,
        )

    ax.set_xticks(x)
    ax.set_xticklabels([f"{r}" for r in rtt_values])
    ax.set_xlabel("RTT (ms)")
    ax.set_ylabel("Small file transfer time (ms)")
    ax.legend(ncol=2)
    fig.tight_layout()

    filename = "small_file.pgf" if use_pgf else "small_file.png"
    out = out_dir / filename
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved {out}")
    plt.close(fig)


def plot_migration_throughput(migration_data: list, out_dir: Path,
                              use_pgf: bool = True,
                              bucket_duration_ms: float = 100.0,
                              path_change_ms: float = 500.0) -> None:
    """
    Plot throughput over time for the migration scenario.
    Each entry in migration_data is a dict with keys:
      - "label":  display name
      - "color":  hex color
      - "runs":   list of chunk lists (only the first is plotted; n=1 in practice)
    """
    fig, ax = plt.subplots(figsize=DEFAULT_FIGSIZE)

    for series in migration_data:
        runs = series["runs"]
        if not runs or not runs[0]:
            continue

        chunks = runs[0]
        total_ms = max(c["t_ms"] for c in chunks)
        n_buckets = max(1, int(np.ceil(total_ms / bucket_duration_ms)))

        bucket_bytes = np.zeros(n_buckets)
        for c in chunks:
            idx = min(int(c["t_ms"] / bucket_duration_ms), n_buckets - 1)
            bucket_bytes[idx] += c["bytes"]

        times      = [(i + 0.5) * bucket_duration_ms / 1000 for i in range(n_buckets)]
        throughput = [(b * 8) / (bucket_duration_ms * 1e3) for b in bucket_bytes]

        ax.plot(times, throughput,
                marker="o", markersize=4,
                label=series["label"],
                color=series["color"],
                linewidth=1.5)

    ax.axvline(x=path_change_ms / 1000, color="red", linestyle="--", linewidth=1.5,
                label=rf"Path blocked ($T = {path_change_ms/1000:.0f}\,\mathrm{{s}}$)")

    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Throughput (Mbps)")
    ax.set_ylim(bottom=0, top=8)
    ax.legend()

    fig.tight_layout()
    filename = "migration_throughput.pgf" if use_pgf else "migration_throughput.png"
    out = out_dir / filename
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved {out}")
    plt.close(fig)


def collect_handshake(data):
    """result[rtt_ms][test_name] = list of large_file handshake_time_ms"""
    result = {}
    for rtt_entry in data["rtt_results"]:
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

    for name in HANDSHAKE_TEST_NAMES:
        label, color = SERIES[HANDSHAKE_TEST_NAMES[name]]

        # Thicker / solid line for racing, thinner dashed for non-racing
        lw     = 2.5 if "racing" in name else 1.5
        ls     = "--" if "taps" in name else "-"
        marker = "o" if "racing" in name else "s"

        medians = [np.median(handshake[rtt].get(name, [np.nan])) for rtt in rtt_values]

        ax.plot(rtt_values, medians, marker=marker, markersize=4,
                label=label, color=color, linewidth=lw, linestyle=ls, zorder=3)

    # Annotate the minimum 250ms stagger offset as a callout
    min_handshake   = min(handshake.keys())
    tcp_native_time = np.median(handshake[min_handshake].get("tcp_handshake_test", [np.nan]))
    tcp_racing_time = np.median(handshake[min_handshake].get("taps_handshake_tcp",  [np.nan]))
    tcp_stagger = ((tcp_racing_time - tcp_native_time) // 250) * 250
    ax.annotate(
        rf"$+{tcp_stagger:.0f}\,\mathrm{{ms}}$" + "\n(stagger delay)",
        xy=(min_handshake, tcp_racing_time),
        xytext=(min_handshake + 10, tcp_racing_time + 80),
        arrowprops=dict(arrowstyle="->", color="gray"),
        fontsize=8, color="gray"
    )

    ax.set_xlabel("RTT (ms)")
    ax.set_ylabel("Handshake time (ms)")
    ax.set_xticks(rtt_values)
    ax.legend()

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
        "quic_native_dual_ip":      2 * rtt_value,
        "taps_racing_quic_dual_ip": 250 + 2 * 100,
    }
    for i, (name, t) in enumerate(theoretical.items()):
        ax.axvline(
            t, color="red", linestyle="--", linewidth=1.5, alpha=0.8,
            label="Theoretical minima" if i == 0 else "_nolegend_"
        )

    for name in DUAL_IP_QUIC_HANDSHAKE:
        label, color = SERIES[name]
        rtts = handshake[rtt_value].get(name, [np.nan])

        plot = sns.kdeplot(
            data=rtts,
            ax=ax,
            label=label,
            color=color,
            fill=True,
            alpha=0.4,
            linewidth=1.0,
            warn_singular=False,
        )
        plot.set_xlim(320, 880)

    ax.set_xlabel("Handshake time (ms)")
    ax.set_ylabel("Probability density")

    ax.legend(loc="upper right")

    fig.tight_layout()

    ext = "pgf" if use_pgf else "png"
    out = out_dir / f"handshake_kde.{ext}"
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved {out}")
    plt.close(fig)


def plot_rtt_ecdf(
    baseline_file: Path,
    ctaps_file: Path,
    out_dir: Path,
    p_cut: float = 99.0,
    use_pgf: bool = True,
) -> None:
    """
    Plot ECDF of RTT for baseline UDP and CTaps, with x-axis
    limited to a given percentile of the combined data, and
    save to out_dir as PGF or PNG.

    Enhancements:
    - Mark p50, p90, p99 on each ECDF curve.
    - Annotate each marker with its percentile and value (µs).
    - If CTaps p99 is outside the x-range, annotate it at the right edge.
    - For Baseline p99, place label further left with an arrow to avoid overlap.
    """

    # Load RTTs (ns → µs)
    rtt_ns_base = np.loadtxt(baseline_file)
    rtt_ns_ctap = np.loadtxt(ctaps_file)

    rtt_us_base = rtt_ns_base / 1e3
    rtt_us_ctap = rtt_ns_ctap / 1e3

    # ECDF helper
    def ecdf(data: np.ndarray):
        x = np.sort(data)
        y = np.arange(1, len(x) + 1) / len(x)
        return x, y

    x_base, y_base = ecdf(rtt_us_base)
    x_ctap, y_ctap = ecdf(rtt_us_ctap)

    # Percentile cutoff based on combined data
    combined = np.concatenate([rtt_us_base, rtt_us_ctap])
    x_cut = np.percentile(combined, p_cut)

    # Lower bound: minimum over both datasets
    x_min = float(min(x_base[0], x_ctap[0]))

    fig, ax = plt.subplots(figsize=DEFAULT_FIGSIZE)

    base_color = "#1565C0"
    ctap_color = "#2E7D32"

    # Plot ECDFs
    ax.plot(x_base, y_base, label="Baseline UDP", color=base_color, linewidth=1.8)
    ax.plot(x_ctap, y_ctap, label="CTaps",        color=ctap_color, linewidth=1.8)

    # Axis labels and limits
    ax.set_xlabel("RTT (µs)")
    ax.set_ylabel("Percentile")

    ax.set_xlim(x_min, x_cut)
    ax.set_ylim(0.0, 1.0)

    ax.grid(True, linestyle="--", alpha=0.4)

    # Percentiles to mark
    percentiles = [50.0, 90.0, 99.0]

    base_markers_x = []
    base_markers_y = []
    ctap_markers_x = []
    ctap_markers_y = []

    # Compute percentiles for annotation logic
    med_b = np.percentile(rtt_us_base, 50.0)
    p90_b = np.percentile(rtt_us_base, 90.0)
    p99_b = np.percentile(rtt_us_base, 99.0)

    med_c = np.percentile(rtt_us_ctap, 50.0)
    p90_c = np.percentile(rtt_us_ctap, 90.0)
    p99_c = np.percentile(rtt_us_ctap, 99.0)

    # Collect markers (only those that lie within x-range)
    for p in percentiles:
        yp = p / 100.0

        xb_p = np.percentile(rtt_us_base, p)
        if x_min <= xb_p <= x_cut:
            base_markers_x.append(xb_p)
            base_markers_y.append(yp)

        xc_p = np.percentile(rtt_us_ctap, p)
        if x_min <= xc_p <= x_cut:
            ctap_markers_x.append(xc_p)
            ctap_markers_y.append(yp)

    # Scatter the percentile markers
    ax.scatter(
        base_markers_x,
        base_markers_y,
        color=base_color,
        marker="o",
        s=20,
        zorder=3,
        label="_nolegend_",  # avoid duplicate legend entries
    )
    ax.scatter(
        ctap_markers_x,
        ctap_markers_y,
        color=ctap_color,
        marker="s",
        s=20,
        zorder=3,
        label="_nolegend_",
    )

    if p99_c > x_cut:
        ax.annotate(
            f"CTaps p99 $\\approx$ {p99_c:.2f} $\\mu$s\n(outside x-range)",
            xy=(x_cut, 0.99),
            xycoords=("data", "data"),
            xytext=(-5, -25),
            textcoords="offset points",
            ha="right",
            va="top",
            fontsize=8,
            arrowprops=dict(
                arrowstyle="->",
                linewidth=1.0,
            ),
        )


    leg = ax.legend(
        loc="lower right",
        fontsize=8,
        framealpha=0.8,
    )


    fig.tight_layout()

    stats_text = (
        r"\setlength{\tabcolsep}{4pt}"  # default is 6pt
        r"\begin{tabular}{lrrr}"
        r"      & \multicolumn{3}{c}{/ \si{\micro\second}} \\"
        r"      & p50 & p90 & p99 \\"
        rf"Base  & {med_b:0.1f} & {p90_b:0.1f} & {p99_b:0.1f} \\"
        rf"CTaps & {med_c:0.1f} & {p90_c:0.1f} & {p99_c:0.1f}"
        r"\end{tabular}"
    )

    ax.text(
        0.98, 0.20,
        stats_text,
        transform=ax.transAxes,
        ha="right",
        va="bottom",
        fontsize=7,
        bbox=dict(
            boxstyle="round,pad=0.25",
            facecolor="white",
            edgecolor="gray",
            alpha=0.8,
        ),
    )

    out_dir.mkdir(parents=True, exist_ok=True)
    filename = "udp_overhead.pgf" if use_pgf else "udp_overhead.png"
    out = out_dir / filename
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"Saved {out}")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description="Plot CTaps benchmark results")
    parser.add_argument("input", help="Path to benchmark JSON file")
    parser.add_argument("--output", default=".", help="Output directory for plots")
    parser.add_argument("--migration",      action="store_true", help="Whether to plot as migration scenario")
    parser.add_argument("--dual-handshake", action="store_true", help="Whether to plot as dual-handshake scenario")
    parser.add_argument("--small-file",     action="store_true", help="Whether to plot small file transfer")
    parser.add_argument("--handshake",      action="store_true", help="Whether to plot simple handshake scenario")
    parser.add_argument("--rtt-sweep",      action="store_true", help="Whether to plot rtt sweep scenario")
    parser.add_argument("--udp-overhead", nargs=1, metavar=('ctaps'), help="Whether to plot UDP ping overhead")
    parser.add_argument("--bucket-duration-ms", type=float, default=100.0,
                        help="Duration of each throughput bucket in ms (default: 100)")
    parser.add_argument("--path-change-ms", type=float, default=500.0,
                        help="When the path change was triggered in ms (default: 500)")
    parser.add_argument("--pgf", action="store_true", default=False,
                        help="Use PGF backend for LaTeX-quality plots")
    args = parser.parse_args()

    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    configure_style(args.pgf)

    if args.udp_overhead:
        ctaps = args.udp_overhead[0]
        print(f"Using {args.input} as baseline and {ctaps} as ctaps")
        plot_rtt_ecdf(
            baseline_file=Path(args.input),
            ctaps_file=Path(ctaps),
            out_dir=out_dir,
            use_pgf=args.pgf
        )
        return

    data = load(args.input)

    if args.migration:
        migration_data = []
        runs_by_name = defaultdict(list)
        for rtt_entry in data.get("rtt_results", []):
            for test in rtt_entry.get("tests", []):
                name = test.get("test_name")
                if name not in SERIES:
                    continue
                chunks = test.get("large_file", {}).get("buckets")
                if not chunks:
                    continue
                runs_by_name[name].append(chunks)
        for name, runs in runs_by_name.items():
            label, color = SERIES[name]
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
                use_pgf=args.pgf,
            )
        else:
            print("Warning: no usable migration data found in migration file")
    else:
        collected = collect(data)

        if args.small_file:
            plot_small_file(collected, out_dir, use_pgf=args.pgf)

        handshake = collect_handshake(data)
        if args.handshake:
            plot_racing_handshake(handshake, out_dir, use_pgf=args.pgf)

        if args.dual_handshake:
            plot_handshake_kde(handshake, out_dir, use_pgf=args.pgf)


if __name__ == "__main__":
    main()
