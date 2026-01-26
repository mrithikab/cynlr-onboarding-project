import pandas as pd

# config
fn = "pair_metrics.csv"
T = 1000000  # per-pixel budget in ns

# read
df = pd.read_csv(fn)

cols = ['proc0_ns','proc1_ns','inter_output_delta_ns']
print("Rows:", len(df))

# basic stats
for c in cols:
    s = df[c].dropna()
    print(f"\n{c}:")
    print("  mean:", s.mean())
    print("  min:", s.min())
    print("  max:", s.max())
    print("  median:", s.median())
    print("  95%:", s.quantile(0.95))
    print("  99%:", s.quantile(0.99))

# pass fractions per pixel
pass_proc0 = (df['proc0_ns'] <= T).mean()
pass_proc1 = (df['proc1_ns'] <= T).mean()
both_pass = ((df['proc0_ns'] <= T) & (df['proc1_ns'] <= T)).mean()
print("\nPass fractions (<= T):")
print("  proc0:", pass_proc0)
print("  proc1:", pass_proc1)
print("  both:", both_pass)

# counts
total = len(df)
count_proc0 = (df['proc0_ns'] <= T).sum()
print("\nCounts:")
print("  proc0 pass count:", count_proc0, "/", total)