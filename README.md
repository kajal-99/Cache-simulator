# Cache Simulator

A configurable multi-level cache simulator written in **C++17**, supporting
direct-mapped, N-way set-associative, and fully associative L1/L2 cache
hierarchies with LRU replacement and three prefetch algorithms including a
**Markov-based prefetcher**.

---

## Features

| Feature | Details |
|---|---|
| Cache levels | L1 only, or L1 + L2 hierarchy |
| Associativity | Direct-mapped, N-way set-associative, fully associative |
| Replacement policy | LRU (Least Recently Used) |
| Write policy | Write-back with dirty-bit tracking |
| Prefetch algorithms | `plus1`, `markov` (history-based), `hybrid` |
| Markov predictor | Frequency-ranked predictions, LRU row eviction, configurable table size |
| Metrics reported | Hit/miss rate, write-backs, prefetch accuracy, AAT (Average Access Time) |
| Experiment automation | Python sweep across all configurations → CSV output + charts |

---

## Build

```bash
make
```

Requires g++ with C++17 support (`-std=c++17`). Tested on Linux and macOS.

---

## Run

```bash
./cachesim [options] < trace_file
```

### Options

| Flag | Description | Default |
|------|-------------|---------|
| `-c <int>` | log2 of L1 cache size in bytes (e.g. `14` = 16 KB) | 14 |
| `-b <int>` | log2 of block size in bytes (e.g. `5` = 32 B) | 5 |
| `-s <int>` | log2 of L1 associativity (e.g. `2` = 4-way) | 0 |
| `-C <int>` | log2 of L2 cache size in bytes | 17 |
| `-S <int>` | log2 of L2 associativity | 0 |
| `-D` | Disable L2 (single-level mode) | — |
| `-F <algo>` | Prefetch algorithm: `none`, `plus1`, `markov`, `hybrid` | `none` |
| `-r <int>` | Markov table rows (used with `markov` / `hybrid`) | 0 |

### Examples

```bash
# L1 only — 16 KB, 4-way, 32 B blocks
./cachesim -c 14 -b 5 -s 2 -D < traces/sample.trace

# L1 + L2 — no prefetcher
./cachesim -c 14 -b 5 -s 2 -C 17 -S 3 < traces/sample.trace

# L1 + L2 — Markov prefetcher, 64-row table
./cachesim -c 14 -b 5 -s 2 -C 17 -S 3 -F markov -r 64 < traces/sample.trace

# L1 + L2 — Hybrid prefetcher (Markov + next-line fallback)
./cachesim -c 14 -b 5 -s 2 -C 17 -S 3 -F hybrid -r 128 < traces/sample.trace
```

---

## Trace Format

One memory access per line:

```
r 0x7fff1234
w 0x0040a8b0
r 0x0040a8c0
```

Where `r` = read and `w` = write, followed by a hex address.

---

## Run All Experiments

Sweeps across all cache sizes, associativity levels, block sizes, replacement
policies, and prefetch configurations across six benchmark traces:

```bash
python3 run_experiments.py
```

Output: `results.csv`

---

## Generate Charts

```bash
pip install matplotlib numpy
python3 plot_results.py
```

Saves four chart types to `results/`:

| Chart | What it shows |
|---|---|
| `l1_only_*` | L1-only AAT vs associativity per block size |
| `l1l2_nopf_*` | L1+L2 hierarchy AAT vs block size per trace |
| `prefetcher_comparison_best_aat.png` | Best AAT: no prefetch vs Plus1 vs Markov vs Hybrid |
| `markov_table_size_vs_aat.png` | Effect of Markov table size on AAT across traces |

---

## Architecture

```
cachesim.hpp          Cache hierarchy types, constants, simulator API
cachesim.cpp          Core simulation: LRU sets, L1/L2 access logic,
                      write-back eviction, prefetch engine, Markov predictor
main.cpp              Argument parsing, trace reader, statistics reporter
run_experiments.py    Automated parameter sweep → CSV
plot_results.py       CSV → matplotlib charts
Makefile              Build system
```

### Markov Prefetcher Design

The Markov prefetcher maintains a history table mapping each cache miss
address to its most likely successor block address. On each L2 miss:

1. The table is queried for the current miss block address
2. The highest-frequency prediction is issued as a prefetch
3. The table is updated with the observed `(previous → current)` transition
4. LRU eviction manages the table size when it exceeds the configured row limit

The **Hybrid** mode uses Markov when a table entry exists, falling back to
next-line (`plus1`) prefetching for cold addresses — combining Markov's
accuracy with plus1's coverage.

---

## Benchmark Traces

Six memory access traces used for evaluation:

| Trace | Workload type |
|---|---|
| `gcc` | Compiler — irregular access patterns |
| `leela` | Go game engine — pointer-heavy |
| `linpack` | Dense linear algebra — highly regular |
| `matmul_naive` | Naive matrix multiply — poor locality |
| `matmul_tiled` | Tiled matrix multiply — cache-optimized |
| `mcf` | Network flow — sparse, irregular |

---

## Results

Experiments were run across six benchmark traces: `gcc`, `leela`, `linpack`, 
`matmul_naive`, `matmul_tiled`, and `mcf`.

Key observations:
- Increasing L1 associativity from 1-way to 4-way consistently reduced AAT 
  for irregular access patterns (gcc, leela)
- The Hybrid prefetcher outperformed both Plus1 and standalone Markov on 
  sparse traces (mcf) by falling back to next-line prefetching for cold addresses
- Tiled matrix multiply showed significantly lower AAT than naive, confirming 
  the impact of spatial locality on cache performance
- Markov table sizes beyond 128 rows showed diminishing returns on most traces

To reproduce results, run `python3 run_experiments.py` after building.

## Tech Stack

C++17 &nbsp;·&nbsp; Python 3 &nbsp;·&nbsp; matplotlib &nbsp;·&nbsp; Make
