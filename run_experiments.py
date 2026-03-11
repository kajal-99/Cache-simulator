import subprocess
import csv
import os

# ── Configuration ──────────────────────────────────────────────
TRACE_DIR = "traces"
OUTPUT_CSV = "results.csv"

# Cache size as log2 bytes: 14=16KB, 15=32KB
L1_SIZES   = [14, 15]
L2_SIZES   = [16, 17]       # 64KB, 128KB
BLOCK_SIZES = [5, 6, 7]     # 32B, 64B, 128B
ASSOCS      = [0, 1, 2, 3]  # 1-way, 2-way, 4-way, 8-way
PREFETCHERS = ["none", "plus1", "markov", "hybrid"]
MARKOV_ROWS = [16, 64, 128, 256]

# Auto-detect available traces
TRACES = [
    os.path.splitext(f)[0]
    for f in os.listdir(TRACE_DIR)
    if f.endswith(".trace")
]

# ── Simulator runner ───────────────────────────────────────────
def run(args, trace):
    """Run cachesim and return L1 AAT, or None on failure."""
    cmd = f"./cachesim {args} < {TRACE_DIR}/{trace}.trace"
    try:
        out = subprocess.check_output(cmd, shell=True, stderr=subprocess.DEVNULL).decode()
        for line in out.splitlines():
            if "L1 average access time" in line:
                return line.split(":")[-1].strip()
    except subprocess.CalledProcessError:
        return None
    return None

# ── Experiment sweep ───────────────────────────────────────────
os.makedirs("results", exist_ok=True)

with open(OUTPUT_CSV, "w", newline="") as f:
    writer = csv.writer(f)
    writer.writerow([
        "Scenario", "Trace", "L1_size", "Block", "L1_assoc",
        "L2_size", "L2_assoc", "Prefetcher", "Markov_rows", "AAT"
    ])

    for trace in TRACES:
        print(f"Trace: {trace}")

        # ── Scenario 1: L1 only ──
        for b in BLOCK_SIZES:
            for c1 in L1_SIZES:
                for s1 in ASSOCS:
                    args = f"-c {c1} -b {b} -s {s1} -D"
                    aat = run(args, trace)
                    if aat:
                        writer.writerow([
                            "L1_only", trace, c1, b, s1,
                            "-", "-", "none", "-", aat
                        ])

        # ── Scenario 2: L1 + L2, no prefetcher ──
        for b in BLOCK_SIZES:
            for c1 in L1_SIZES:
                for s1 in ASSOCS:
                    for c2 in L2_SIZES:
                        for s2 in ASSOCS:
                            if c2 <= c1 or s2 < s1:
                                continue
                            args = f"-c {c1} -b {b} -s {s1} -C {c2} -S {s2} -F none"
                            aat = run(args, trace)
                            if aat:
                                writer.writerow([
                                    "L1_L2_NoPF", trace, c1, b, s1,
                                    c2, s2, "none", "-", aat
                                ])

        # ── Scenario 3: L1 + L2 with prefetchers ──
        for b in BLOCK_SIZES:
            for c1 in L1_SIZES:
                for s1 in ASSOCS:
                    for c2 in L2_SIZES:
                        for s2 in ASSOCS:
                            if c2 <= c1 or s2 < s1:
                                continue
                            for pf in ["plus1", "markov", "hybrid"]:
                                rows_list = MARKOV_ROWS if pf in ["markov", "hybrid"] else [0]
                                for r in rows_list:
                                    args = f"-c {c1} -b {b} -s {s1} -C {c2} -S {s2} -F {pf} -r {r}"
                                    aat = run(args, trace)
                                    if aat:
                                        writer.writerow([
                                            "L1_L2_WithPF", trace, c1, b, s1,
                                            c2, s2, pf, r, aat
                                        ])

print(f"\nDone. Results saved to {OUTPUT_CSV}")
