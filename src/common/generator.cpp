#include "sigscan/generator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <stdexcept>

namespace sigscan {

namespace {

constexpr double kMiB = 1024.0 * 1024.0;

// Separate RNG stream per purpose, so e.g. patterns don't change with text size.
std::mt19937_64 stream(u64 seed, u64 id) { return std::mt19937_64(splitmix64(seed ^ splitmix64(id))); }

// 64x64 -> 128-bit product (MSVC has no __int128)
struct Wide {
    u64 hi, lo;
};
Wide mul_wide(u64 a, u64 b) {
#if defined(__SIZEOF_INT128__)
    const unsigned __int128 m = (unsigned __int128)a * b;
    return {u64(m >> 64), u64(m)};
#else
    const u64 a0 = a & 0xFFFFFFFFu, a1 = a >> 32, b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    const u64 p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const u64 mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    return {p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32), (mid << 32) | (p00 & 0xFFFFFFFFu)};
#endif
}

// Own uniform (Lemire) instead of std::*_distribution, which differs between compilers.
// mt19937_64 itself is fully specified, so the same seed gives the same data everywhere.
u64 uniform(std::mt19937_64& rng, u64 lo, u64 hi) {  // inclusive
    const u64 range = hi - lo + 1;
    if (range == 0) return rng();  // full 64-bit range
    Wide m = mul_wide(rng(), range);
    if (m.lo < range) {
        const u64 threshold = (u64(0) - range) % range;
        while (m.lo < threshold) m = mul_wide(rng(), range);
    }
    return lo + m.hi;
}

double uniform01(std::mt19937_64& rng) { return double(rng() >> 11) * 0x1.0p-53; }

std::vector<u8> make_alphabet(const std::string& a, bool no_match) {
    std::vector<u8> sym;
    if (a == "full") {
        for (int c = 0; c < 256; ++c) sym.push_back(u8(c));
    } else if (a == "ascii") {
        for (int c = 0x20; c < 0x7F; ++c) sym.push_back(u8(c));
    } else if (a == "dna") {
        sym = {'A', 'C', 'G', 'T'};
    } else {
        const long k = std::stol(a);
        if (k < 2 || k > 256) throw std::runtime_error("--alphabet N must be in 2..256");
        for (long c = 0; c < k; ++c) sym.push_back(u8(c));
    }
    // no-match: text bytes < 0x80, every pattern starts with a byte >= 0x80
    if (no_match) std::erase_if(sym, [](u8 c) { return c >= 0x80; });
    return sym;
}

struct Item {
    u64 pos;
    u32 pid;
    u32 len;   // bytes written
    u8 kind;   // 0 = hit, 1 = boundary hit, 2 = partial
};

}  // namespace

GenOutput generate(const GenParams& p) {
    if (p.npat == 0) throw std::runtime_error("--npat must be > 0");
    if (p.minlen == 0 || p.minlen > p.maxlen) throw std::runtime_error("need 0 < minlen <= maxlen");
    GenOutput g;
    const std::vector<u8> sym = make_alphabet(p.alphabet, p.no_match);
    const u64 k = sym.size();

    // --- patterns
    {
        auto rng = stream(p.seed, 1);
        auto rand_sym = [&] { return sym[uniform(rng, 0, k - 1)]; };
        std::vector<std::vector<u8>> prefixes(p.prefix_groups);
        for (auto& pre : prefixes) {
            pre.resize(p.prefix_len);
            for (auto& c : pre) c = rand_sym();
        }
        for (u32 i = 0; i < p.npat; ++i) {
            const u32 len = u32(uniform(rng, p.minlen, p.maxlen));
            std::vector<u8> pat(len);
            for (auto& c : pat) c = rand_sym();
            if (p.prefix_groups && uniform01(rng) < p.prefix_share) {
                const auto& pre = prefixes[uniform(rng, 0, p.prefix_groups - 1)];
                std::copy_n(pre.begin(), std::min<std::size_t>(pre.size(), len > 1 ? len - 1 : 0), pat.begin());
            }
            if (p.no_match) pat[0] = u8(0x80 + uniform(rng, 0, 127));
            g.pats.add(std::move(pat));
        }
    }

    // --- background noise
    g.text.resize(p.size);
    {
        auto rng = stream(p.seed, 2);
        u8* t = g.text.data();
        const u64 n = p.size;
        if (k == 256) {
            u64 i = 0;
            for (; i + 8 <= n; i += 8) {
                const u64 x = rng();
                for (int b = 0; b < 8; ++b) t[i + b] = u8(x >> (8 * b));
            }
            for (const u64 x = rng(); i < n; ++i) t[i] = u8(x >> (8 * (i & 7)));
        } else {
            // 32-bit Lemire, bias negligible
            u64 i = 0;
            for (; i + 2 <= n; i += 2) {
                const u64 x = rng();
                t[i] = sym[(u64(u32(x)) * k) >> 32];
                t[i + 1] = sym[((x >> 32) * k) >> 32];
            }
            if (i < n) t[i] = sym[(u64(u32(rng())) * k) >> 32];
        }
    }
    if (p.no_match) return g;

    // --- injected occurrences
    const u64 n = p.size;
    auto rng = stream(p.seed, 3);
    std::vector<u32> crossable;  // patterns longer than 1 byte
    for (u32 i = 0; i < g.pats.size(); ++i)
        if (g.pats.pats[i].size() >= 2) crossable.push_back(i);

    std::vector<Item> fixed;  // boundary hits, placed first
    if (p.boundary_block && !crossable.empty()) {
        for (u64 b = p.boundary_block; b < n; b += p.boundary_block) {
            const u32 pid = crossable[uniform(rng, 0, crossable.size() - 1)];
            const u32 len = u32(g.pats.pats[pid].size());
            const u64 j = uniform(rng, 1, len - 1);  // bytes before the border
            if (b < j || b - j + len > n) continue;
            fixed.push_back({b - j, pid, len, 1});
        }
    }

    std::vector<Item> random_items;
    const double hot_w = p.hot_len ? p.hot_mult * double(p.hot_len) : 0.0;
    const double hot_prob = hot_w > 0 ? hot_w / (hot_w + double(n - std::min(n, p.hot_len))) : 0.0;
    auto random_pos = [&](u32 len) -> u64 {
        if (len > n) return n;  // does not fit
        if (hot_prob > 0 && uniform01(rng) < hot_prob) {
            const u64 hs = std::min(p.hot_start, n), he = std::min(p.hot_start + p.hot_len, n);
            if (he - hs >= len) return uniform(rng, hs, he - len);
        }
        return uniform(rng, 0, n - len);
    };
    const u64 nhits = u64(std::llround(p.hit_rate * double(n) / kMiB));
    for (u64 h = 0; h < nhits; ++h) {
        const u32 pid = u32(uniform(rng, 0, g.pats.size() - 1));
        const u32 len = u32(g.pats.pats[pid].size());
        if (u64 pos = random_pos(len); pos + len <= n) random_items.push_back({pos, pid, len, 0});
    }
    const u64 npart = crossable.empty() ? 0 : u64(std::llround(p.partial_rate * double(n) / kMiB));
    for (u64 h = 0; h < npart; ++h) {
        const u32 pid = crossable[uniform(rng, 0, crossable.size() - 1)];
        const u32 plen = u32(uniform(rng, 1, g.pats.pats[pid].size() - 1));  // matched prefix length
        if (u64 pos = random_pos(plen + 1); pos + plen + 1 <= n) random_items.push_back({pos, pid, plen + 1, 2});
    }
    // stable: std::sort order of equal keys differs between compilers
    std::stable_sort(random_items.begin(), random_items.end(), [](const Item& a, const Item& b) { return a.pos < b.pos; });

    // place without overlaps, boundary hits first
    std::vector<Item> placed;
    {
        u64 last_end = 0;
        for (const Item& it : fixed) {
            if (it.pos < last_end) { ++g.dropped; continue; }
            placed.push_back(it);
            last_end = it.pos + it.len;
        }
        std::vector<Item> kept;
        std::size_t f = 0;
        last_end = 0;
        for (const Item& it : random_items) {
            while (f < placed.size() && placed[f].pos + placed[f].len <= it.pos) ++f;
            const bool hits_fixed = f < placed.size() && placed[f].pos < it.pos + it.len;
            if (it.pos < last_end || hits_fixed) { ++g.dropped; continue; }
            kept.push_back(it);
            last_end = it.pos + it.len;
        }
        placed.insert(placed.end(), kept.begin(), kept.end());
    }

    for (const Item& it : placed) {
        const auto& pat = g.pats.pats[it.pid];
        u8* dst = g.text.data() + it.pos;
        if (it.kind == 2) {
            const u32 plen = it.len - 1;
            std::copy_n(pat.begin(), plen, dst);
            u8 c = sym[uniform(rng, 0, k - 1)];
            if (c == pat[plen]) c = sym[(std::find(sym.begin(), sym.end(), c) - sym.begin() + 1) % k];
            dst[plen] = c;  // break the match right after the prefix
            ++g.injected_partial;
        } else {
            std::copy(pat.begin(), pat.end(), dst);
            g.truth.push_back({it.pid, it.pos});
            ++(it.kind == 1 ? g.injected_boundary : g.injected_hits);
        }
    }
    // no overlaps, so every truth entry stays intact
    std::sort(g.truth.begin(), g.truth.end(), match_less);
    return g;
}

std::string meta_json(const GenParams& p, const GenOutput& g) {
    char buf[2048];
    std::snprintf(
        buf, sizeof buf,
        "{\n"
        "  \"generator\": \"sigscan-gen\",\n  \"version\": 1,\n  \"seed\": %llu,\n  \"size\": %llu,\n"
        "  \"npat\": %u,\n  \"minlen\": %u,\n  \"maxlen\": %u,\n  \"hit_rate\": %g,\n  \"partial_rate\": %g,\n"
        "  \"prefix_share\": %g,\n  \"prefix_len\": %u,\n  \"prefix_groups\": %u,\n  \"alphabet\": \"%s\",\n"
        "  \"boundary_block\": %llu,\n  \"no_match\": %s,\n  \"hot_start\": %llu,\n  \"hot_len\": %llu,\n"
        "  \"hot_mult\": %g,\n  \"injected_hits\": %llu,\n  \"injected_boundary\": %llu,\n"
        "  \"injected_partial\": %llu,\n  \"dropped\": %llu,\n  \"pattern_total_len\": %zu\n}\n",
        (unsigned long long)p.seed, (unsigned long long)p.size, p.npat, p.minlen, p.maxlen, p.hit_rate,
        p.partial_rate, p.prefix_share, p.prefix_len, p.prefix_groups, p.alphabet.c_str(),
        (unsigned long long)p.boundary_block, p.no_match ? "true" : "false", (unsigned long long)p.hot_start,
        (unsigned long long)p.hot_len, p.hot_mult, (unsigned long long)g.injected_hits,
        (unsigned long long)g.injected_boundary, (unsigned long long)g.injected_partial,
        (unsigned long long)g.dropped, g.pats.total_len);
    return buf;
}

}  // namespace sigscan
