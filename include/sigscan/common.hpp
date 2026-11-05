// Types shared by all versions.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#if defined(__CUDACC__)
#define SIGSCAN_HD __host__ __device__
#else
#define SIGSCAN_HD
#endif

namespace sigscan {

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// One match: pattern id + start offset.
struct Match {
    u32 pid;
    u64 pos;
    friend bool operator==(const Match&, const Match&) = default;
};

inline bool match_less(const Match& a, const Match& b) {
    return a.pos != b.pos ? a.pos < b.pos : a.pid < b.pid;
}

SIGSCAN_HD inline u64 splitmix64(u64 x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// Order-independent checksum: sum of H(pid) * (pos + K) mod 2^64, H odd.
// Linear in pos, so count mode can use per-state sums instead of walking output chains.
inline constexpr u64 kChecksumK = 0x632BE59BD9B4E019ULL;

SIGSCAN_HD inline u64 pattern_hash(u32 pid) { return splitmix64(pid) | 1ULL; }

SIGSCAN_HD inline u64 match_term(u32 pid, u64 pos) { return pattern_hash(pid) * (pos + kChecksumK); }

// Every version must give exactly this.
struct Summary {
    u64 count = 0;
    u64 checksum = 0;
    void add(const Summary& o) {
        count += o.count;
        checksum += o.checksum;
    }
    friend bool operator==(const Summary&, const Summary&) = default;
};

Summary summarize(const std::vector<Match>& m);

}  // namespace sigscan
