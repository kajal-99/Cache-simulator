/**
 * main.cpp
 * Entry point for the cache simulator.
 * Parses command-line arguments, reads a memory-access trace from stdin,
 * drives the simulation, and prints a formatted statistics report.
 *
 * Trace format (one access per line):
 *   <r|w> <hex-address>
 *   e.g.  r 0x7fff1234
 */

#include "cachesim.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>

// ---------------------------------------------------------------------------
// Print usage / help
// ---------------------------------------------------------------------------
static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] < trace_file\n"
        "\n"
        "L1 Cache options:\n"
        "  -c <int>   log2 of L1 cache size in bytes  (e.g. 14 = 16 KB)\n"
        "  -b <int>   log2 of block size in bytes      (e.g.  5 = 32 B)\n"
        "  -s <int>   log2 of L1 associativity         (e.g.  2 = 4-way)\n"
        "\n"
        "L2 Cache options (all optional):\n"
        "  -C <int>   log2 of L2 cache size in bytes  (e.g. 17 = 128 KB)\n"
        "  -S <int>   log2 of L2 associativity         (e.g.  3 = 8-way)\n"
        "  -D         disable L2 (single-level mode)\n"
        "\n"
        "Prefetch options:\n"
        "  -F <algo>  prefetch algorithm: none | plus1 | markov | hybrid\n"
        "  -r <int>   Markov table rows (used with markov / hybrid)\n"
        "\n"
        "Examples:\n"
        "  # L1 only, 16 KB, 4-way, 32 B blocks:\n"
        "  %s -c 14 -b 5 -s 2 -D < gcc.trace\n"
        "\n"
        "  # L1 + L2, Markov prefetcher, 64 rows:\n"
        "  %s -c 14 -b 5 -s 2 -C 17 -S 3 -F markov -r 64 < gcc.trace\n",
        prog, prog, prog);
}

// ---------------------------------------------------------------------------
// Parse a prefetch algorithm name
// ---------------------------------------------------------------------------
static prefetch_algo_t parse_prefetch(const char *name) {
    if (strcmp(name, "plus1")  == 0) return PREFETCH_PLUS_ONE;
    if (strcmp(name, "markov") == 0) return PREFETCH_MARKOV;
    if (strcmp(name, "hybrid") == 0) return PREFETCH_HYBRID;
    return PREFETCH_NONE;
}

// ---------------------------------------------------------------------------
// Print a formatted statistics report
// ---------------------------------------------------------------------------
static void print_stats(const sim_stats_t *s, bool l2_enabled) {
    printf("================ Cache Simulation Results ================\n");
    printf("\n--- L1 Cache ---\n");
    printf("  Total accesses  : %llu\n",  (unsigned long long)s->accesses_l1);
    printf("  Reads           : %llu\n",  (unsigned long long)s->reads);
    printf("  Writes          : %llu\n",  (unsigned long long)s->writes);
    printf("  Hits            : %llu\n",  (unsigned long long)s->hits_l1);
    printf("  Misses          : %llu\n",  (unsigned long long)s->misses_l1);
    printf("  Write-backs     : %llu\n",  (unsigned long long)s->write_backs_l1);
    printf("  Hit ratio       : %.4f\n",  s->hit_ratio_l1);
    printf("  Miss ratio      : %.4f\n",  s->miss_ratio_l1);
    printf("  L1 average access time: %.4f cycles\n", s->avg_access_time_l1);

    if (l2_enabled) {
        printf("\n--- L2 Cache ---\n");
        printf("  Reads           : %llu\n",  (unsigned long long)s->reads_l2);
        printf("  Read hits       : %llu\n",  (unsigned long long)s->read_hits_l2);
        printf("  Read misses     : %llu\n",  (unsigned long long)s->read_misses_l2);
        printf("  Read hit ratio  : %.4f\n",  s->read_hit_ratio_l2);
        printf("  Read miss ratio : %.4f\n",  s->read_miss_ratio_l2);
        printf("  L2 average access time: %.4f cycles\n", s->avg_access_time_l2);

        if (s->prefetches_issued_l2 > 0) {
            printf("\n--- Prefetcher ---\n");
            printf("  Prefetches issued : %llu\n", (unsigned long long)s->prefetches_issued_l2);
            printf("  Prefetch hits     : %llu\n", (unsigned long long)s->prefetch_hits_l2);
            printf("  Prefetch misses   : %llu\n", (unsigned long long)s->prefetch_misses_l2);
            double acc = (s->prefetches_issued_l2 > 0)
                ? (double)s->prefetch_hits_l2 / s->prefetches_issued_l2
                : 0.0;
            printf("  Prefetch accuracy : %.4f\n", acc);
        }
    }
    printf("==========================================================\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {

    // ---- Default configuration ----
    sim_config_t cfg = {};
    cfg.l1_config.c  = 14;   // 16 KB
    cfg.l1_config.b  = 5;    // 32 B blocks
    cfg.l1_config.s  = 0;    // direct-mapped
    cfg.l1_config.disabled          = false;
    cfg.l1_config.prefetch_algorithm = PREFETCH_NONE;
    cfg.l1_config.n_markov_rows      = 0;

    cfg.l2_config.c  = 17;   // 128 KB
    cfg.l2_config.b  = 5;
    cfg.l2_config.s  = 0;
    cfg.l2_config.disabled          = false;
    cfg.l2_config.prefetch_algorithm = PREFETCH_NONE;
    cfg.l2_config.n_markov_rows      = 0;

    // ---- Argument parsing ----
    int opt;
    while ((opt = getopt(argc, argv, "c:b:s:C:S:DF:r:h")) != -1) {
        switch (opt) {
            case 'c': cfg.l1_config.c = atoi(optarg); break;
            case 'b':
                cfg.l1_config.b = atoi(optarg);
                cfg.l2_config.b = atoi(optarg);   // same block size for both levels
                break;
            case 's': cfg.l1_config.s = atoi(optarg); break;
            case 'C': cfg.l2_config.c = atoi(optarg); break;
            case 'S': cfg.l2_config.s = atoi(optarg); break;
            case 'D': cfg.l2_config.disabled = true;  break;
            case 'F': cfg.l2_config.prefetch_algorithm = parse_prefetch(optarg); break;
            case 'r': cfg.l2_config.n_markov_rows = (uint64_t)atoi(optarg);     break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    // ---- Initialise simulator ----
    sim_stats_t stats = {};
    sim_setup(&cfg);

    // ---- Read trace from stdin ----
    char  rw;
    uint64_t addr;
    while (scanf(" %c %lx", &rw, &addr) == 2) {
        sim_access(rw, addr, &stats);
    }

    // ---- Finalise and report ----
    sim_finish(&stats);
    print_stats(&stats, !cfg.l2_config.disabled);

    return 0;
}
