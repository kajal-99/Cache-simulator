"""
plot_results.py
Reads comprehensive_aat_results.csv produced by run_experiments.py and
generates a set of publication-ready comparison charts saved to results/.
"""

import csv
import os
import collections
import matplotlib
matplotlib.use("Agg")          # headless — no display needed
import matplotlib.pyplot as plt
import matplotlib.cm as cm
import numpy as np

CSV_FILE    = "comprehensive_aat_results.csv"
RESULTS_DIR = "results"
os.makedirs(RESULTS_DIR, exist_ok=True)

# ---------------------------------------------------------------------------
# Load CSV
# ---------------------------------------------------------------------------
rows = []
with open(CSV_FILE, newline="") as f:
    reader = csv.DictReader(f)
    for r in reader:
        try:
            r["AAT"] = float(r["AAT"])
            r["C1"]  = int(r["C1"])
            r["B"]   = int(r["B"])
            r["S1"]  = int(r["S1"])
            r["C2"]  = int(r["C2"])
            r["S2"]  = int(r["S2"])
            r["R"]   = int(r["R"])
            rows.append(r)
        except (ValueError, KeyError):
            continue   # skip header or malformed rows

# ---------------------------------------------------------------------------
# Helper: human-readable labels
# ---------------------------------------------------------------------------
def cache_size_label(c_bits):
    kb = (1 << c_bits) // 1024
    return f"{kb} KB"

def assoc_label(s_bits):
    if s_bits == 0:
        return "Direct-mapped"
    return f"{1 << s_bits}-way"

# ---------------------------------------------------------------------------
# Chart 1: L1-Only — AAT vs Associativity for each cache size
#          (one sub-plot per block size, one bar group per trace)
# ---------------------------------------------------------------------------
l1_rows = [r for r in rows if r["Scenario"] == "L1_Only"]
traces   = sorted(set(r["Trace"] for r in l1_rows))
b_values = sorted(set(r["B"] for r in l1_rows))

for b in b_values:
    subset = [r for r in l1_rows if r["B"] == b]
    c1_vals = sorted(set(r["C1"] for r in subset))
    s1_vals = sorted(set(r["S1"] for r in subset))

    fig, axes = plt.subplots(1, len(c1_vals), figsize=(6 * len(c1_vals), 5), sharey=False)
    if len(c1_vals) == 1:
        axes = [axes]
    fig.suptitle(f"L1-Only: AAT vs Associativity  (block = {1 << b} B)", fontsize=14, fontweight="bold")

    colors = cm.tab10(np.linspace(0, 0.9, len(traces)))

    for ax, c1 in zip(axes, c1_vals):
        x     = np.arange(len(s1_vals))
        width = 0.8 / len(traces)
        for ti, (trace, color) in enumerate(zip(traces, colors)):
            aats = []
            for s1 in s1_vals:
                match = [r["AAT"] for r in subset if r["C1"] == c1 and r["S1"] == s1 and r["Trace"] == trace]
                aats.append(min(match) if match else 0)
            ax.bar(x + ti * width, aats, width, label=trace, color=color)

        ax.set_title(f"L1 = {cache_size_label(c1)}", fontsize=11)
        ax.set_xlabel("Associativity")
        ax.set_ylabel("AAT (cycles)")
        ax.set_xticks(x + width * (len(traces) - 1) / 2)
        ax.set_xticklabels([assoc_label(s) for s in s1_vals])
        ax.legend(fontsize=7, ncol=2)
        ax.grid(axis="y", linestyle="--", alpha=0.5)

    plt.tight_layout()
    out = os.path.join(RESULTS_DIR, f"l1_only_b{b}_aat_vs_assoc.png")
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Saved: {out}")

# ---------------------------------------------------------------------------
# Chart 2: L1+L2 No-Prefetch — AAT vs Block Size
#          grouped by (C1, S1) pair, one plot per trace
# ---------------------------------------------------------------------------
hier_rows = [r for r in rows if r["Scenario"] == "L1_L2_NoPF"]

for trace in traces:
    subset = [r for r in hier_rows if r["Trace"] == trace]
    if not subset:
        continue

    b_vals  = sorted(set(r["B"] for r in subset))
    configs = sorted(set((r["C1"], r["S1"], r["C2"], r["S2"]) for r in subset))[:8]  # cap at 8 for readability

    x      = np.arange(len(b_vals))
    width  = 0.8 / len(configs)
    colors = cm.Set2(np.linspace(0, 1, len(configs)))

    fig, ax = plt.subplots(figsize=(10, 5))
    for ci, (cfg, color) in enumerate(zip(configs, colors)):
        c1, s1, c2, s2 = cfg
        label = f"L1={cache_size_label(c1)}/{assoc_label(s1)}, L2={cache_size_label(c2)}/{assoc_label(s2)}"
        aats  = []
        for b in b_vals:
            match = [r["AAT"] for r in subset if r["B"] == b and r["C1"] == c1 and r["S1"] == s1 and r["C2"] == c2 and r["S2"] == s2]
            aats.append(min(match) if match else 0)
        ax.bar(x + ci * width, aats, width, label=label, color=color)

    ax.set_title(f"L1+L2 Hierarchy (No Prefetch) — {trace} trace", fontsize=12, fontweight="bold")
    ax.set_xlabel("Block Size")
    ax.set_ylabel("AAT (cycles)")
    ax.set_xticks(x + width * (len(configs) - 1) / 2)
    ax.set_xticklabels([f"{1 << b} B" for b in b_vals])
    ax.legend(fontsize=7, loc="upper right", ncol=1)
    ax.grid(axis="y", linestyle="--", alpha=0.5)

    plt.tight_layout()
    out = os.path.join(RESULTS_DIR, f"l1l2_nopf_{trace}_aat_vs_blocksize.png")
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Saved: {out}")

# ---------------------------------------------------------------------------
# Chart 3: Prefetcher Comparison — None vs Plus1 vs Markov vs Hybrid
#          best AAT per prefetcher per trace (bar chart)
# ---------------------------------------------------------------------------
pf_rows = [r for r in rows if r["Scenario"] in ("L1_L2_NoPF", "L1_L2_WithPF")]

pf_labels = ["none", "plus1", "markov", "hybrid"]
pf_colors = ["#4C72B0", "#55A868", "#C44E52", "#8172B2"]

# Find best (minimum) AAT per (trace, prefetcher) combination
best = collections.defaultdict(lambda: collections.defaultdict(list))
for r in pf_rows:
    pf = r.get("Prefetcher", "none")
    best[r["Trace"]][pf].append(r["AAT"])

fig, ax = plt.subplots(figsize=(12, 5))
x      = np.arange(len(traces))
width  = 0.8 / len(pf_labels)

for pi, (pf, color) in enumerate(zip(pf_labels, pf_colors)):
    aats = [min(best[t][pf]) if best[t][pf] else 0 for t in traces]
    ax.bar(x + pi * width, aats, width, label=pf.capitalize(), color=color)

ax.set_title("Prefetcher Comparison: Best AAT per Trace", fontsize=13, fontweight="bold")
ax.set_xlabel("Trace")
ax.set_ylabel("Minimum AAT (cycles)")
ax.set_xticks(x + width * (len(pf_labels) - 1) / 2)
ax.set_xticklabels(traces, rotation=15)
ax.legend(title="Prefetcher", fontsize=9)
ax.grid(axis="y", linestyle="--", alpha=0.5)

plt.tight_layout()
out = os.path.join(RESULTS_DIR, "prefetcher_comparison_best_aat.png")
plt.savefig(out, dpi=150, bbox_inches="tight")
plt.close()
print(f"Saved: {out}")

# ---------------------------------------------------------------------------
# Chart 4: Markov Table Size vs AAT (markov + hybrid prefetchers)
# ---------------------------------------------------------------------------
markov_rows = [r for r in rows if r["Prefetcher"] in ("markov", "hybrid") and r["R"] > 0]

if markov_rows:
    fig, axes = plt.subplots(1, 2, figsize=(14, 5), sharey=False)
    for ax, pf in zip(axes, ["markov", "hybrid"]):
        subset   = [r for r in markov_rows if r["Prefetcher"] == pf]
        r_values = sorted(set(r["R"] for r in subset))
        colors   = cm.tab10(np.linspace(0, 0.9, len(traces)))

        for trace, color in zip(traces, colors):
            aats = []
            for rv in r_values:
                match = [r["AAT"] for r in subset if r["Trace"] == trace and r["R"] == rv]
                aats.append(min(match) if match else None)
            valid_x = [rv for rv, a in zip(r_values, aats) if a is not None]
            valid_y = [a  for a      in aats              if a is not None]
            if valid_x:
                ax.plot(valid_x, valid_y, marker="o", label=trace, color=color)

        ax.set_title(f"{pf.capitalize()} Prefetcher: AAT vs Markov Table Rows", fontsize=11, fontweight="bold")
        ax.set_xlabel("Markov Table Rows")
        ax.set_ylabel("AAT (cycles)")
        ax.legend(fontsize=8)
        ax.grid(linestyle="--", alpha=0.5)

    plt.tight_layout()
    out = os.path.join(RESULTS_DIR, "markov_table_size_vs_aat.png")
    plt.savefig(out, dpi=150, bbox_inches="tight")
    plt.close()
    print(f"Saved: {out}")

print(f"\nAll charts saved to '{RESULTS_DIR}/'")
