#!/usr/bin/env python3
"""Simple analyzer for pair_metrics.csv to detect latency spikes.

Usage:
  python analyze_pair_metrics.py --file x64/Release/pair_metrics.csv

Outputs:
  - pair_metrics_analysis.png : time series plots with spikes highlighted
  - pair_metrics_spikes.csv   : rows flagged as spikes
"""
import argparse
from pathlib import Path
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns


def detect_spikes(df, col, z_thresh=3.0, pctile=0.99, window=31, rolling_mult=5.0):
    s = df[col].astype(float)
    mean = s.mean()
    std = s.std()
    q = s.quantile(pctile)

    # z-score spikes
    z = (s - mean) / (std if std != 0 else 1.0)
    spikes_z = z.abs() > z_thresh

    # percentile spikes
    spikes_pct = s > q

    # rolling deviation spikes (local spikes)
    if window < 3:
        window = 3
    rolling_med = s.rolling(window=window, center=True, min_periods=1).median()
    # median absolute deviation (robust)
    mad = (s - rolling_med).abs().rolling(window=window, center=True, min_periods=1).median()
    spikes_local = (s - rolling_med) > (rolling_mult * (mad.replace(0, np.nan).fillna(mad.mean())))

    flags = spikes_z | spikes_pct | spikes_local
    return flags.fillna(False)


def analyze(path: Path, out_prefix="pair_metrics", time_col=None):
    df = pd.read_csv(path)

    # If there's a time column, try to parse, else use index
    if time_col and time_col in df.columns:
        try:
            df[time_col] = pd.to_datetime(df[time_col])
            df = df.set_index(time_col)
        except Exception:
            pass

    numeric_cols = df.select_dtypes(include=[np.number]).columns.tolist()
    if not numeric_cols:
        # try converting any columns to numeric if possible
        for c in df.columns:
            df[c] = pd.to_numeric(df[c], errors='ignore')
        numeric_cols = df.select_dtypes(include=[np.number]).columns.tolist()

    if not numeric_cols:
        raise SystemExit("No numeric columns found in the CSV to analyze.")

    spike_masks = pd.DataFrame(index=df.index)

    # Only meaningful to compute percentiles for timing-related columns
    pctile_cols = {"queue_latency_ns", "proc0_ns", "proc1_ns"}
    # Columns to run spike detection on (timing-related)
    spike_cols = {"queue_latency_ns", "proc0_ns", "proc1_ns", "inter_output_delta_ns"}

    summary = {}
    for col in numeric_cols:
        col_vals = df[col].dropna().astype(float)
        if col in pctile_cols:
            stats = col_vals.describe(percentiles=[0.5, 0.95, 0.99]).to_dict()
        else:
            stats = {
                'count': int(col_vals.count()),
                'mean': float(col_vals.mean()) if col_vals.size > 0 else float('nan'),
                'min': float(col_vals.min()) if col_vals.size > 0 else float('nan'),
                'max': float(col_vals.max()) if col_vals.size > 0 else float('nan')
            }
        summary[col] = stats
        if col in spike_cols:
            flags = detect_spikes(df, col)
        else:
            flags = pd.Series(False, index=df.index)
        spike_masks[col] = flags

    # Any row that has at least one spike
    if spike_masks.shape[1] > 0:
        any_spike = spike_masks.any(axis=1)
    else:
        any_spike = pd.Series(False, index=df.index)

    # Select context columns to keep in spike output (no heavy stats for these)
    context_cols = [c for c in ["seq", "gen_ts_ns", "gen_ts_valid", "pop_ts_ns", "proc_start_ns", "out0_ts_ns", "out1_ts_ns"] if c in df.columns]
    timing_cols = [c for c in ["queue_latency_ns", "proc0_ns", "proc1_ns", "inter_output_delta_ns"] if c in df.columns]

    spikes_df = df.loc[any_spike, context_cols + timing_cols]
    spikes_out = f"{out_prefix}_spikes.csv"
    spikes_df.to_csv(spikes_out, index=True)

    # Plotting
    sns.set(style="darkgrid", rc={"figure.figsize": (14, max(4, 1.5*len(numeric_cols)))})
    fig, axes = plt.subplots(len(numeric_cols), 1, sharex=True)
    if len(numeric_cols) == 1:
        axes = [axes]

    for ax, col in zip(axes, numeric_cols):
        series = df[col].astype(float)
        series.plot(ax=ax, label=col)
        # plot spikes
        mask = spike_masks[col].fillna(False)
        if mask.any():
            ax.scatter(series.index[mask], series[mask], color='red', s=20, label='spike')
        ax.set_ylabel(col)
        ax.legend(loc='upper right')

    plt.tight_layout()
    out_plot = f"{out_prefix}_analysis.png"
    plt.savefig(out_plot, dpi=150)

    # Print a concise summary to stdout (only timing columns)
    print("Timing summary:")
    for col in timing_cols:
        stats = summary.get(col)
        if stats and '50%' in stats:
            print(f"- {col}: p50={stats.get('50%'):.3g} p95={stats.get('95%'):.3g} p99={stats.get('99%'):.3g} max={stats.get('max'):.3g}")

    # Show top spikes by queue latency if present
    if 'queue_latency_ns' in spikes_df.columns and not spikes_df.empty:
        top = spikes_df.sort_values('queue_latency_ns', ascending=False).head(10)
        print('\nTop spikes (by queue_latency_ns):')
        print(top.to_string(index=False))

    print(f"Spike rows: {len(spikes_df)} -> saved to {spikes_out}")
    print(f"Plot: {out_plot}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--file', '-f', required=False, default='x64/Release/pair_metrics.csv', help='Path to pair_metrics.csv')
    p.add_argument('--time-col', help='Optional time column name to use as index')
    p.add_argument('--out-prefix', default='pair_metrics', help='Output filename prefix')
    args = p.parse_args()

    analyze(Path(args.file), out_prefix=args.out_prefix, time_col=args.time_col)


if __name__ == '__main__':
    main()
