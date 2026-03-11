import subprocess
import csv

# Define the search space based on project constraints and TA clarifications
l1_sizes = [14, 15]      # 16KB, 32KB
l2_sizes = [16, 17]      # 64KB, 128KB
block_sizes = [5, 6, 7]  # 32B, 64B, 128B
assocs = [1, 2, 3]       # 2-way, 4-way, 8-way (S=1, 2, 3)
policies = ["mip", "lip"]
prefetchers = ["plus1", "markov", "hybrid"]
markov_rows = [16, 32, 64, 128, 256, 512]

traces = ["gcc", "leela", "linpack", "matmul_naive", "matmul_tiled", "mcf"]

def run_simulation(args, trace):
    """Executes the simulator and extracts the L1 AAT from the output."""
    cmd = f"./cachesim {args} < traces/{trace}.trace"
    try:
        out = subprocess.check_output(cmd, shell=True).decode()
        for line in out.split("\n"):
            if "L1 average access time" in line:
                return line.split(":")[-1].strip()
    except Exception:
        return None
    return None

# Output file will categorize results by Scenario
with open('comprehensive_aat_results.csv', 'w') as f:
    writer = csv.writer(f)
    writer.writerow(['Scenario', 'Trace', 'C1', 'B', 'S1', 'C2', 'S2', 'Policy', 'Prefetcher', 'R', 'AAT'])

    for trace in traces:
        print(f"Processing trace: {trace}...")

        # --- SCENARIO 1: L1 ONLY (L2 Disabled) ---
        for b in block_sizes:
            for c1 in l1_sizes:
                for s1 in assocs:
                    # -D flag disables L2 according to your main.cpp
                    args = f"-c {c1} -b {b} -s {s1} -D"
                    aat = run_simulation(args, trace)
                    if aat:
                        writer.writerow(['L1_Only', trace, c1, b, s1, 0, 0, 'none', 'none', 0, aat])

        # --- SCENARIOS 2 & 3: L1 + L2 Hierarchy ---
        for b in block_sizes:
            for c1 in l1_sizes:
                for s1 in assocs:
                    for c2 in l2_sizes:
                        for s2 in assocs:
                            # Constraints: L1 Size < L2 Size and L1 Assoc <= L2 Assoc
                            if c2 <= c1 or s2 < s1:
                                continue

                            for p in policies:
                                # Scenario 2: Baseline Hierarchy (No Prefetcher)
                                args_no_pf = f"-c {c1} -b {b} -s {s1} -C {c2} -S {s2} -P {p} -F none -r 0"
                                aat_no_pf = run_simulation(args_no_pf, trace)
                                if aat_no_pf:
                                    writer.writerow(['L1_L2_NoPF', trace, c1, b, s1, c2, s2, p, 'none', 0, aat_no_pf])

                                # Scenario 3: Optimized Hierarchy (With Prefetchers)
                                for pf in prefetchers:
                                    # Rows relevant for Markov and Hybrid
                                    r_list = markov_rows if pf in ["markov", "hybrid"] else [0]
                                    for r in r_list:
                                        args_pf = f"-c {c1} -b {b} -s {s1} -C {c2} -S {s2} -P {p} -F {pf} -r {r}"
                                        aat_pf = run_simulation(args_pf, trace)
                                        if aat_pf:
                                            writer.writerow(['L1_L2_WithPF', trace, c1, b, s1, c2, s2, p, pf, r, aat_pf])

print("Experiments complete. Results saved to comprehensive_aat_results.csv")