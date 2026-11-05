// Data generator with known ground truth.
#pragma once

#include <string>
#include <vector>

#include "sigscan/common.hpp"
#include "sigscan/patterns.hpp"

namespace sigscan {

struct GenParams {
    u64 size = 64ULL << 20;     // text bytes
    u32 npat = 1000;
    u32 minlen = 8, maxlen = 32;
    double hit_rate = 10;       // injected full occurrences per MiB
    double partial_rate = 0;    // injected pattern prefixes per MiB
    double prefix_share = 0;    // fraction of patterns with a shared prefix
    u32 prefix_len = 4;
    u32 prefix_groups = 8;
    std::string alphabet = "full";  // full | ascii | dna | N (bytes 0..N-1)
    u64 boundary_block = 0;     // > 0: one hit across every multiple of this
    bool no_match = false;      // guaranteed zero matches
    u64 hot_start = 0, hot_len = 0;
    double hot_mult = 1;        // more hits inside the hot region
    u64 seed = 2026;
};

struct GenOutput {
    std::vector<u8> text;
    PatternSet pats;
    std::vector<Match> truth;  // injected hits, sorted
    u64 injected_hits = 0, injected_boundary = 0, injected_partial = 0, dropped = 0;
};

GenOutput generate(const GenParams& p);
std::string meta_json(const GenParams& p, const GenOutput& g);

}  // namespace sigscan
