#pragma once

#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// Word and timing constants
// ---------------------------------------------------------------------------
#define WORD_SIZE           8       // bytes per word
#define L1_HIT_TIME_CONST   2.0     // L1 base hit time (cycles)
#define L1_HIT_TIME_PER_S   0.5     // L1 hit time per associativity bit
#define L2_HIT_TIME_CONST   10.0    // L2 base hit time (cycles)
#define L2_HIT_TIME_PER_S   1.0     // L2 hit time per associativity bit
#define DRAM_AT             100.0   // DRAM base access time (cycles)
#define DRAM_AT_PER_WORD    2.0     // DRAM additional time per word

// ---------------------------------------------------------------------------
// Access type
// ---------------------------------------------------------------------------
#define READ    'r'
#define WRITE   'w'

// ---------------------------------------------------------------------------
// Prefetch algorithm selection
// ---------------------------------------------------------------------------
typedef enum {
    PREFETCH_NONE    = 0,
    PREFETCH_PLUS_ONE,
    PREFETCH_MARKOV,
    PREFETCH_HYBRID
} prefetch_algo_t;

// ---------------------------------------------------------------------------
// Cache configuration (log2-encoded sizes)
//   c = log2(total cache size in bytes)
//   b = log2(block size in bytes)
//   s = log2(associativity)
// ---------------------------------------------------------------------------
typedef struct {
    int             c;                  // log2 of cache size
    int             b;                  // log2 of block size
    int             s;                  // log2 of associativity
    bool            disabled;           // true → cache is not present
    prefetch_algo_t prefetch_algorithm; // prefetch policy for this level
    uint64_t        n_markov_rows;      // Markov table capacity (rows)
} cache_config_t;

// ---------------------------------------------------------------------------
// Simulator configuration
// ---------------------------------------------------------------------------
typedef struct {
    cache_config_t l1_config;
    cache_config_t l2_config;
} sim_config_t;

// ---------------------------------------------------------------------------
// Simulation statistics collected during a run
// ---------------------------------------------------------------------------
typedef struct {
    // Overall access counts
    uint64_t accesses_l1;
    uint64_t reads;
    uint64_t writes;

    // L1 statistics
    uint64_t hits_l1;
    uint64_t misses_l1;
    uint64_t write_backs_l1;
    double   hit_ratio_l1;
    double   miss_ratio_l1;
    double   avg_access_time_l1;

    // L2 statistics
    uint64_t reads_l2;
    uint64_t writes_l2;
    uint64_t read_hits_l2;
    uint64_t read_misses_l2;
    double   read_hit_ratio_l2;
    double   read_miss_ratio_l2;
    double   avg_access_time_l2;

    // Prefetch statistics
    uint64_t prefetches_issued_l2;
    uint64_t prefetch_hits_l2;
    uint64_t prefetch_misses_l2;
} sim_stats_t;

// ---------------------------------------------------------------------------
// Simulator API  (implemented in cachesim.cpp)
// ---------------------------------------------------------------------------
void sim_setup(sim_config_t *config);
void sim_access(char rw, uint64_t addr, sim_stats_t *stats);
void sim_finish(sim_stats_t *stats);
