#include "cachesim.hpp"
#include <vector>
#include <list>
#include <cstddef>

/* Global structures for cache blocks and sets */
struct Block {
    bool valid;
    bool dirty;
    bool prefetch;
    uint64_t tag;
};

struct Set {
    std::list<Block> blocks;
};

/* Markov Predictor structures */
struct Prediction {
    uint64_t next_block_addr;
    uint32_t frequency;
};

struct MarkovRow {
    uint64_t source_tag; 
    std::vector<Prediction> predictions; 
};

/* L1 Cache State */
static std::vector<Set> L1;
static int L1_num_sets = 0;
static int L1_assoc = 0;
static int L1_offset_bits = 0;
static int L1_index_bits = 0;
static int L1_s = 0;

/* L2 Cache State */
static bool L2_enabled = true;
static std::vector<Set> L2;
static int L2_num_sets = 0;
static int L2_assoc = 0;
static int L2_offset_bits = 0;
static int L2_index_bits = 0;
static int L2_s = 0;
static prefetch_algo_t L2_prefetch_algo = PREFETCH_NONE; 
static uint64_t L2_markov_rows = 0;

/* Markov Table State */
static std::list<MarkovRow> MarkovTable; 
static uint64_t last_block_addr = 0;
static bool last_block_valid = false;

/**
 * Subroutine for initializing the cache simulator. You many add and initialize any global or heap
 * variables as needed.
 * TODO: You're responsible for completing this routine
 */
void sim_setup(sim_config_t *config) {
    const cache_config_t &l1 = config->l1_config;
    const cache_config_t &l2 = config->l2_config;

    // L1 geometry calculations
    L1_offset_bits = l1.b;
    L1_index_bits = l1.c - l1.b - l1.s;
    L1_num_sets = 1 << L1_index_bits;
    L1_assoc = 1 << l1.s;
    L1_s = l1.s;

    // L2 geometry calculations
    L2_offset_bits = l2.b;
    L2_index_bits = l2.c - l2.b - l2.s;
    L2_num_sets = 1 << L2_index_bits;
    L2_assoc = 1 << l2.s;
    L2_s = l2.s;
    L2_prefetch_algo = l2.prefetch_algorithm;
    L2_markov_rows = l2.n_markov_rows;
    L2_enabled = !l2.disabled;

    L1.assign(L1_num_sets, Set()); 
    L2.assign(L2_num_sets, Set());

    MarkovTable.clear(); 
    last_block_valid = false;
}

/**
 * Helper to get the block address (tag + index) by stripping the offset.
 */
static uint64_t get_block_addr(uint64_t addr, int offset_bits) {
    return addr >> offset_bits;
}

/**
 * Logic for L2 accesses.
 * Returns true on hit, false on miss.
 */
static bool l2_access(char rw, uint64_t addr, sim_stats_t* stats) {
    if (!L2_enabled) {
        if (rw == READ) {
            stats->reads_l2++;
            stats->read_misses_l2++;
        } else {
            stats->writes_l2++;
        }
        return false;
    }

    uint64_t index = (addr >> L2_offset_bits) & ((1ULL << L2_index_bits) - 1);
    uint64_t tag = addr >> (L2_offset_bits + L2_index_bits);

    Set &set = L2[index];
    (rw == READ) ? stats->reads_l2++ : stats->writes_l2++;

    // Check for a hit in the L2 set
    for (auto it = set.blocks.begin(); it != set.blocks.end(); ++it) {
        if (it->valid && it->tag == tag) {
            if (rw == READ) {
                stats->read_hits_l2++;
                // If we hit a block brought in by the prefetcher, record the success
                if (it->prefetch) {
                    stats->prefetch_hits_l2++;
                    it->prefetch = false; 
                }
            }
            set.blocks.splice(set.blocks.begin(), set.blocks, it); 
            return true;
        }
    }

    // L2 Miss Logic
    if (rw == READ) {
        stats->read_misses_l2++;
        if ((int)set.blocks.size() == L2_assoc) {
            Block &victim = set.blocks.back();
            if (victim.prefetch) stats->prefetch_misses_l2++;
            set.blocks.pop_back(); 
        }
        Block b = {true, false, false, tag};
        set.blocks.push_back(b); // LIP: Insert at LRU position
    }
    return false;
}

/**
 * Validates and issues a prefetch request to the L2 cache.
 * Checks L1 and L2 for redundancy before insertion.
 */
static void issue_l2_prefetch(uint64_t pf_addr, sim_stats_t* stats) {
    if (!L2_enabled) return;

    // Check L1 first - if it's already there, do nothing
    uint64_t l1_idx = (pf_addr >> L1_offset_bits) & ((1ULL << L1_index_bits) - 1);
    uint64_t l1_tag = pf_addr >> (L1_offset_bits + L1_index_bits);
    for (auto &b : L1[l1_idx].blocks) if (b.valid && b.tag == l1_tag) return;

    // Check L2 - if it's already there, do nothing
    uint64_t l2_idx = (pf_addr >> L2_offset_bits) & ((1ULL << L2_index_bits) - 1);
    uint64_t l2_tag = pf_addr >> (L2_offset_bits + L2_index_bits);
    Set &l2_set = L2[l2_idx];
    for (auto &b : l2_set.blocks) if (b.valid && b.tag == l2_tag) return;

    stats->prefetches_issued_l2++;
    
    if ((int)l2_set.blocks.size() == L2_assoc) {
        Block &victim = l2_set.blocks.back();
        if (victim.prefetch) stats->prefetch_misses_l2++;
        l2_set.blocks.pop_back();
    }
    l2_set.blocks.push_back({true, false, true, l2_tag}); 
}

/**
 * Searches the Markov table for a prediction.
 * Returns the best candidate address based on frequency and address tie-breakers.
 */
static uint64_t get_markov_prediction(uint64_t current_block_addr, bool &row_found) {
    row_found = false;
    for (auto it = MarkovTable.begin(); it != MarkovTable.end(); ++it) {
        if (it->source_tag == current_block_addr) {
            row_found = true;
            uint64_t best_addr = 0;
            uint32_t max_freq = 0;

            for (const auto& pred : it->predictions) {
                if (pred.frequency > max_freq) {
                    max_freq = pred.frequency;
                    best_addr = pred.next_block_addr;
                } else if (pred.frequency == max_freq && pred.next_block_addr > best_addr) {
                    best_addr = pred.next_block_addr; 
                }
            }
            return best_addr;
        }
    }
    return 0;
}

/**
 * Updates the Markov table transitions. 
 * Handles row LRU and frequency-based entry replacement.
 */
static void l2_markov_update(uint64_t current_miss_addr) {
    uint64_t B = get_block_addr(current_miss_addr, L2_offset_bits);
    if (!last_block_valid) {
        last_block_valid = true;
        last_block_addr = B;
        return;
    }

    uint64_t A = last_block_addr;
    last_block_addr = B;

    auto row_it = MarkovTable.end();
    for (auto it = MarkovTable.begin(); it != MarkovTable.end(); ++it) {
        if (it->source_tag == A) {
            row_it = it;
            break;
        }
    }

    if (row_it != MarkovTable.end()) {
        MarkovTable.splice(MarkovTable.begin(), MarkovTable, row_it);
        bool found_B = false;
        for (auto& pred : MarkovTable.begin()->predictions) {
            if (pred.next_block_addr == B) {
                pred.frequency++;
                found_B = true;
                break;
            }
        }

        if (!found_B) {
            if (MarkovTable.begin()->predictions.size() < 4) {
                MarkovTable.begin()->predictions.push_back({B, 1});
            } else {
                std::size_t vic_idx = 0;
                for (std::size_t i = 1; i < 4; ++i) {
                    uint32_t cur_f = MarkovTable.begin()->predictions[i].frequency;
                    uint32_t vic_f = MarkovTable.begin()->predictions[vic_idx].frequency;
                    if (cur_f < vic_f || (cur_f == vic_f && MarkovTable.begin()->predictions[i].next_block_addr < MarkovTable.begin()->predictions[vic_idx].next_block_addr)) {
                        vic_idx = i;
                    }
                }
                MarkovTable.begin()->predictions[vic_idx] = {B, 1};
            }
        }
    } else {
        if (MarkovTable.size() >= L2_markov_rows && L2_markov_rows > 0) MarkovTable.pop_back();
        if (L2_markov_rows > 0) {
            MarkovRow nr;
            nr.source_tag = A;
            nr.predictions.push_back({B, 1});
            MarkovTable.push_front(nr);
        }
    }
}

/**
 * Subroutine that simulates the cache one trace event at a time.
 * TODO: You're responsible for completing this routine
 */
void sim_access(char rw, uint64_t addr, sim_stats_t* stats) {
    stats->accesses_l1++;
    (rw == READ) ? stats->reads++ : stats->writes++;

    uint64_t index = (addr >> L1_offset_bits) & ((1ULL << L1_index_bits) - 1);
    uint64_t tag = addr >> (L1_offset_bits + L1_index_bits);
    Set &set = L1[index];

    // L1 Search
    for (auto it = set.blocks.begin(); it != set.blocks.end(); ++it) {
        if (it->valid && it->tag == tag) {
            stats->hits_l1++;
            if (rw == WRITE) it->dirty = true;
            set.blocks.splice(set.blocks.begin(), set.blocks, it);
            return;
        }
    }

    // L1 Miss Logic
    stats->misses_l1++;
    bool l2_hit = l2_access(READ, addr, stats); 

    // Handle Prefetching based on selected algorithm
    if (!l2_hit && L2_prefetch_algo != PREFETCH_NONE) {
        uint64_t pf_block_addr = 0;
        bool row_exists = false;
        uint64_t curr_block = get_block_addr(addr, L2_offset_bits);

        if (L2_prefetch_algo == PREFETCH_MARKOV) {
            pf_block_addr = get_markov_prediction(curr_block, row_exists);
        } else if (L2_prefetch_algo == PREFETCH_PLUS_ONE) {
            pf_block_addr = curr_block + 1;
        } else if (L2_prefetch_algo == PREFETCH_HYBRID) {
            pf_block_addr = get_markov_prediction(curr_block, row_exists);
            if (!row_exists) pf_block_addr = curr_block + 1;
        }

        if (pf_block_addr != 0 && pf_block_addr != curr_block) {
            issue_l2_prefetch(pf_block_addr << L2_offset_bits, stats);
        }

        if (L2_prefetch_algo == PREFETCH_MARKOV || L2_prefetch_algo == PREFETCH_HYBRID) {
            l2_markov_update(addr);
        }
    }

    // L1 Victim Eviction
    if ((int)set.blocks.size() == L1_assoc) {
        Block &victim = set.blocks.back();
        if (victim.valid && victim.dirty) {
            uint64_t wb_addr = ((victim.tag << L1_index_bits) | index) << L1_offset_bits;
            stats->write_backs_l1++;
            l2_access(WRITE, wb_addr, stats);
        }
        set.blocks.pop_back();
    }

    // Insert new block into L1
    set.blocks.push_front({true, (rw == WRITE), false, tag});
}

/**
 * Subroutine for cleaning up any outstanding memory operations and calculating overall statistics
 * such as miss rate or average access time.
 * TODO: You're responsible for completing this routine
 */
void sim_finish(sim_stats_t *stats) {
    if (stats->accesses_l1 > 0) {
        stats->hit_ratio_l1 = (double)stats->hits_l1 / stats->accesses_l1;
        stats->miss_ratio_l1 = 1.0 - stats->hit_ratio_l1;
    }

    if (stats->reads_l2 > 0) {
        stats->read_hit_ratio_l2 = (double)stats->read_hits_l2 / stats->reads_l2;
        stats->read_miss_ratio_l2 = 1.0 - stats->read_hit_ratio_l2;
    }

    double words = (double)(1 << L1_offset_bits) / WORD_SIZE;
    double dram_time = DRAM_AT + DRAM_AT_PER_WORD * words;

    if (!L2_enabled) {
        stats->avg_access_time_l2 = dram_time;
    } else {
        double h2 = L2_HIT_TIME_CONST + L2_HIT_TIME_PER_S * (double)L2_s;
        stats->avg_access_time_l2 = h2 + stats->read_miss_ratio_l2 * dram_time;
    }

    double h1 = L1_HIT_TIME_CONST + L1_HIT_TIME_PER_S * (double)L1_s;
    stats->avg_access_time_l1 = h1 + stats->miss_ratio_l1 * stats->avg_access_time_l2;
}
