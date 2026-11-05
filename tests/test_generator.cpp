#include <set>

#include <doctest/doctest.h>
#include "helpers.hpp"
#include "sigscan/generator.hpp"

using namespace sigscan;
using namespace sigscan::test;

namespace {

GenParams small_params() {
    GenParams p;
    p.size = 1 << 20;
    p.npat = 200;
    p.minlen = 4;
    p.maxlen = 24;
    p.hit_rate = 300;
    p.partial_rate = 300;
    p.prefix_share = 0.5;
    p.seed = 7;
    return p;
}

std::vector<Match> reference(const GenOutput& g) {
    return scan_all(build_dense(build_trie(g.pats)), g.text);
}

bool contains_all(const std::vector<Match>& all, const std::vector<Match>& sub) {
    const std::set<std::pair<u32, u64>> s = [&] {
        std::set<std::pair<u32, u64>> r;
        for (const auto& m : all) r.insert({m.pid, m.pos});
        return r;
    }();
    for (const auto& m : sub)
        if (!s.count({m.pid, m.pos})) return false;
    return true;
}

}  // namespace

TEST_CASE("generator is deterministic for a seed") {
    const GenParams p = small_params();
    const GenOutput a = generate(p), b = generate(p);
    CHECK(a.text == b.text);
    CHECK(a.pats.pats == b.pats.pats);
    CHECK(a.truth == b.truth);
    GenParams q = p;
    q.seed = 8;
    CHECK(generate(q).text != a.text);
}

TEST_CASE("pattern set does not depend on text size") {
    GenParams p = small_params();
    const auto a = generate(p);
    p.size *= 2;
    CHECK(generate(p).pats.pats == a.pats.pats);
}

TEST_CASE("reference finds every injected occurrence") {
    for (const char* alpha : {"full", "ascii", "dna", "3"}) {
        CAPTURE(alpha);
        GenParams p = small_params();
        p.alphabet = alpha;
        const GenOutput g = generate(p);
        CHECK(g.injected_hits > 0);
        CHECK(g.injected_partial > 0);
        CHECK(contains_all(reference(g), g.truth));
    }
}

TEST_CASE("no-match mode yields zero matches") {
    for (const char* alpha : {"full", "ascii", "dna"}) {
        CAPTURE(alpha);
        GenParams p = small_params();
        p.alphabet = alpha;
        p.no_match = true;
        const GenOutput g = generate(p);
        CHECK(g.truth.empty());
        CHECK(reference(g).empty());
    }
}

TEST_CASE("boundary occurrences are found exactly once with chunking") {
    GenParams p = small_params();
    p.boundary_block = 4096;
    p.hit_rate = 0;
    p.partial_rate = 0;
    const GenOutput g = generate(p);
    CHECK(g.injected_boundary > 200);
    const auto dense = build_dense(build_trie(g.pats));
    const auto chunked = scan_chunked(dense, g.text, p.boundary_block);
    CHECK(chunked == scan_all(dense, g.text));
    CHECK(contains_all(chunked, g.truth));
    for (const auto& m : g.truth) {  // each crosses a border
        const u64 end = m.pos + g.pats.pats[m.pid].size();
        CHECK(m.pos / p.boundary_block != (end - 1) / p.boundary_block);
    }
}

TEST_CASE("hot region concentrates hits") {
    GenParams p = small_params();
    p.partial_rate = 0;
    p.hot_start = 0;
    p.hot_len = p.size / 8;
    p.hot_mult = 20;
    const GenOutput g = generate(p);
    u64 inside = 0;
    for (const auto& m : g.truth) inside += m.pos < p.hot_len;
    CHECK(double(inside) / double(g.truth.size()) > 0.6);
}
