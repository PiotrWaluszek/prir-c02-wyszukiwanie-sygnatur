// Aho-Corasick in three layouts: dense DFA, compressed (bitmap + rank) and the PFAC table for GPU.
// States are in BFS order, root = 0. kFlag on a transition means the target has outputs.
#pragma once

#include <array>
#include <bit>
#include <limits>
#include <vector>

#include "sigscan/common.hpp"
#include "sigscan/patterns.hpp"

namespace sigscan {

inline constexpr u32 kFlag = 0x80000000u;
inline constexpr u32 kMask = 0x7FFFFFFFu;
inline constexpr u32 kNone = std::numeric_limits<u32>::max();

// Outputs per state, same for every layout.
struct Outputs {
    std::vector<u32> depth;      // string length of the state
    std::vector<u32> own_begin;  // CSR: patterns ending exactly here
    std::vector<u32> own_ids;
    std::vector<u64> own_h;      // sum of H over own ids
    std::vector<u32> out_link;   // nearest suffix with outputs, or kNone
    // sums over the whole output chain
    std::vector<u32> total;      // number of patterns
    std::vector<u64> sum_h;      // sum of H(pid)
    std::vector<u64> sum_hl;     // sum of H(pid) * len(pid)

    bool has_own(u32 s) const { return own_begin[s + 1] > own_begin[s]; }
    u32 own_count(u32 s) const { return own_begin[s + 1] - own_begin[s]; }
    std::size_t bytes() const;
};

// Trie with failure links, every layout is built from it.
struct Trie {
    u32 num_states = 0;
    std::size_t max_len = 0;
    std::vector<u32> edge_begin;  // CSR, size S+1, edges sorted by byte
    std::vector<u8> edge_byte;
    std::vector<u32> edge_child;
    std::vector<u32> fail;
    Outputs out;
};

Trie build_trie(const PatternSet& ps);

struct DenseAC {
    u32 num_states = 0;
    std::size_t max_len = 0;
    std::vector<u32> next;  // S*256, flagged target states
    Outputs out;

    u32 step(u32 st, u8 c) const { return next[(std::size_t(st) << 8) | c]; }
    std::size_t bytes() const { return next.size() * sizeof(u32) + out.bytes(); }
};

DenseAC build_dense(const Trie& t);

struct CNode {
    u64 bits[4];  // which bytes have a child
    u32 base;     // index of the first child in CompressedAC::child
    u32 fail;
};

struct CompressedAC {
    u32 num_states = 0;
    std::size_t max_len = 0;
    std::array<u32, 256> root_row{};  // root stays dense, it is used all the time
    std::vector<CNode> nodes;
    std::vector<u32> child;  // children per node, in byte order
    Outputs out;

    u32 step(u32 st, u8 c) const {
        for (;;) {
            if (st == 0) return root_row[c];
            const CNode& nd = nodes[st];
            const unsigned w = c >> 6;
            const u64 bit = 1ULL << (c & 63);
            if (nd.bits[w] & bit) {
                u32 rank = std::popcount(nd.bits[w] & (bit - 1));
                for (unsigned k = 0; k < w; ++k) rank += std::popcount(nd.bits[k]);
                return child[nd.base + rank];
            }
            st = nd.fail;
        }
    }
    std::size_t bytes() const {
        return sizeof(root_row) + nodes.size() * sizeof(CNode) + child.size() * sizeof(u32) + out.bytes();
    }
};

CompressedAC build_compressed(const Trie& t);

// PFAC trie, no failure links. next == 0 means no edge (root is never a child).
struct PfacTable {
    u32 num_states = 0;
    std::size_t max_len = 0;
    std::vector<u32> next;
    Outputs out;

    std::size_t table_bytes() const { return next.size() * sizeof(u32); }
};

PfacTable build_pfac(const Trie& t);

}  // namespace sigscan
