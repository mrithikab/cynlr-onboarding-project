#!/usr/bin/env python3
"""Classify spikes in pair metrics CSV into queue-driven, processing-driven, or idle-waiting.

Usage:
  python classify_spikes.py --file x64/Release/pair_metrics.csv
"""
import argparse
from pathlib import Path
import pandas as pd
import numpy as np


def ns_to_ms(ns):
    return ns / 1e6


def classify(df):
    # Ensure relevant columns exist
    for c in ['queue_latency_ns', 'proc0_ns', 'proc1_ns', 'inter_output_delta_ns', 'gen_ts_ns', 'seq']:
        if c not in df.columns:
            df[c] = 0

    # Convert to numeric
    for c in df.columns:
        df[c] = pd.to_numeric(df[c], errors='coerce').fillna(0)

    # Estimate production period T from gen_ts_ns diffs when valid
    if 'gen_ts_ns' in df.columns and df['gen_ts_ns'].gt(0).sum() > 2:
        dt = df.loc[df['gen_ts_ns'] > 0, 'gen_ts_ns'].diff().abs()
        T_ns = int(dt.median()) if not dt.dropna().empty else 0
    else:
        T_ns = 0

    stats = {}
    stats['queue_p95'] = df['queue_latency_ns'].quantile(0.95)
    stats['queue_p99'] = df['queue_latency_ns'].quantile(0.99)
    stats['proc0_p95'] = df['proc0_ns'].quantile(0.95)
    stats['proc1_p95'] = df['proc1_ns'].quantile(0.95)
    stats['proc0_p99'] = df['proc0_ns'].quantile(0.99)
    stats['proc1_p99'] = df['proc1_ns'].quantile(0.99)
    stats['inter_p95'] = df['inter_output_delta_ns'].quantile(0.95)

    # Basic spike selection: any timing column over its p95 or p99
    is_queue_spike = df['queue_latency_ns'] > stats['queue_p95']
    is_proc_spike = (df['proc0_ns'] > stats['proc0_p99']) | (df['proc1_ns'] > stats['proc1_p99'])
    is_idle_spike = df['inter_output_delta_ns'] > stats['inter_p95']

    # Classify with priority: processing -> queue -> idle -> other
    cls = []
    for i, row in df.iterrows():
        if is_proc_spike.iloc[i]:
            cls.append('processing-driven')
        elif is_queue_spike.iloc[i]:
            cls.append('queue-driven')
        elif is_idle_spike.iloc[i]:
            cls.append('idle-waiting')
        else:
            cls.append('none')

    df['classification'] = cls

    # Analyze contiguous queue-driven runs
    q_idx = df.index[df['classification'] == 'queue-driven']
    runs = []
    if not q_idx.empty:
        start = q_idx[0]
        prev = start
        length = 1
        for idx in q_idx[1:]:
            if idx == prev + 1:
                length += 1
            else:
                runs.append((start, prev, length))
                start = idx
                length = 1
            prev = idx
        runs.append((start, prev, length))

    run_stats = {
        'num_queue_runs': len(runs),
        'max_run_length': max((r[2] for r in runs), default=0),
        'avg_run_length': (sum(r[2] for r in runs) / len(runs)) if runs else 0,
        'runs': runs,
        'T_ns': T_ns
    }

    return df, stats, run_stats


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--file', '-f', default='x64/Release/pair_metrics.csv')
    p.add_argument('--top', type=int, default=5)
    args = p.parse_args()

    path = Path(args.file)
    if not path.exists():
        print('File not found:', path)
        return

    df = pd.read_csv(path)
    classified, stats, run_stats = classify(df)

    print('Computed thresholds:')
    for k, v in stats.items():
        print(f' - {k}: {v:.0f} ns')

    counts = classified['classification'].value_counts()
    print('\nSpike classification counts:')
    print(counts.to_string())

    print('\nQueue-run stats:')
    print(f" - number of queue-driven runs: {run_stats['num_queue_runs']}")
    print(f" - max run length (rows): {run_stats['max_run_length']}")
    if run_stats['T_ns']:
        max_ms = ns_to_ms(run_stats['max_run_length'] * run_stats['T_ns'])
        print(f" - estimated max run duration: {max_ms:.3f} ms (using median gen delta)")

    # Show top examples per class
    for cls in ['queue-driven', 'processing-driven', 'idle-waiting']:
        sub = classified[classified['classification'] == cls]
        if sub.empty:
            print(f"\nNo examples for {cls}")
            continue
        if cls == 'queue-driven':
            top = sub.sort_values('queue_latency_ns', ascending=False).head(args.top)
            keycol = 'queue_latency_ns'
        elif cls == 'processing-driven':
            top = sub.assign(maxproc=sub[['proc0_ns', 'proc1_ns']].max(axis=1)).sort_values('maxproc', ascending=False).head(args.top)
            keycol = 'maxproc'
        else:
            top = sub.sort_values('inter_output_delta_ns', ascending=False).head(args.top)
            keycol = 'inter_output_delta_ns'

        print(f"\nTop {args.top} examples for {cls} (showing seq, gen_ts_ns, pop_ts_ns, proc_start_ns, {keycol}):")
        cols = [c for c in ['seq', 'gen_ts_ns', 'pop_ts_ns', 'proc_start_ns', keycol] if c in top.columns]
        print(top[cols].to_string(index=False))

    out = Path('pair_metrics_classified.csv')
    classified.to_csv(out, index=False)
    print(f"\nClassified rows saved to {out}")


if __name__ == '__main__':
    main()
