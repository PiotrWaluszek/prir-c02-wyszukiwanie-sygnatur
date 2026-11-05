#include <random>

#include <doctest/doctest.h>
#include "helpers.hpp"

using namespace sigscan;
using namespace sigscan::test;

namespace {

// Every layout and split must match the brute-force oracle.
void check_all(const PatternSet& ps, const std::vector<u8>& text, const std::vector<Match>* expected = nullptr) {
    const auto want = naive(ps, text);
    if (expected) REQUIRE(want == sorted(*expected));
    const Summary want_sum = summarize(want);

    const Trie trie = build_trie(ps);
    const DenseAC dense = build_dense(trie);
    const CompressedAC comp = build_compressed(trie);
    const PfacTable pf = build_pfac(trie);

    CHECK(scan_all(dense, text) == want);
    CHECK(scan_all(comp, text) == want);
    CHECK(pfac_all(pf, text) == want);
    CHECK(count_all(dense, text) == want_sum);
    CHECK(count_all(comp, text) == want_sum);
    for (u64 chunk : {1, 2, 3, 5, 7, 8, 13, 64}) {
        CAPTURE(chunk);
        CHECK(scan_chunked(dense, text, chunk) == want);
        CHECK(scan_chunked(comp, text, chunk) == want);
        CHECK(count_chunked(dense, text, chunk) == want_sum);
    }
    for (int lanes : {2, 4, 8, 16}) {
        for (u64 chunk : {u64(text.size()) + 1, u64(13), u64(64), u64(101)}) {
            CAPTURE(lanes);
            CAPTURE(chunk);
            ListSink l;
            scan_lanes_chunked(dense, text, chunk, lanes, l);
            CHECK(sorted(l.matches) == want);
            CountSink c;
            scan_lanes_chunked(dense, text, chunk, lanes, c);
            CHECK(c.summary() == want_sum);
            ListSink lc;
            scan_lanes_chunked(comp, text, chunk, lanes, lc);
            CHECK(sorted(lc.matches) == want);
        }
    }
}

}  // namespace

TEST_CASE("classic he/she/his/hers") {
    const auto ps = make_patterns({"he", "she", "his", "hers"});
    const std::vector<Match> exp{{1, 1}, {0, 2}, {3, 2}};
    check_all(ps, bytes("ushers"), &exp);
}

TEST_CASE("overlapping occurrences are all reported") {
    const auto ps = make_patterns({"aa"});
    const std::vector<Match> exp{{0, 0}, {0, 1}, {0, 2}};
    check_all(ps, bytes("aaaa"), &exp);
}

TEST_CASE("pattern is prefix, suffix and infix of another") {
    const auto ps = make_patterns({"abcd", "ab", "cd", "bc", "abcdab"});
    check_all(ps, bytes("xxabcdabcdabyy"));
}

TEST_CASE("duplicate patterns get separate ids") {
    const auto ps = make_patterns({"abc", "abc", "bc"});
    const std::vector<Match> exp{{0, 1}, {1, 1}, {2, 2}};
    check_all(ps, bytes("zabc"), &exp);
}

TEST_CASE("binary bytes 0x00, 0xFF and newline") {
    PatternSet ps;
    ps.add({0x00, 0xFF});
    ps.add({0xFF, 0x00, 0xFF});
    ps.add({'\n'});
    const std::vector<u8> text{0x00, 0xFF, 0x00, 0xFF, '\n', 0x00};
    check_all(ps, text);
}

TEST_CASE("single-byte pattern, pattern longer than text, empty text") {
    check_all(make_patterns({"a"}), bytes("banana"));
    check_all(make_patterns({"abcdefgh"}), bytes("abc"));
    check_all(make_patterns({"abc"}), {});
    check_all(make_patterns({"abc"}), bytes("abc"));
}

TEST_CASE("chunk smaller than the longest pattern") {
    const auto ps = make_patterns({"abcdefghij", "efg", "j"});
    check_all(ps, bytes("..abcdefghij..abcdefghij"));
}

TEST_CASE("random small-alphabet cases (property test)") {
    std::mt19937_64 rng(12345);
    for (int iter = 0; iter < 200; ++iter) {
        CAPTURE(iter);
        const int sigma = 2 + int(rng() % 3);
        PatternSet ps;
        const int npat = 1 + int(rng() % 12);
        for (int i = 0; i < npat; ++i) {
            std::vector<u8> p(1 + rng() % 7);
            for (auto& c : p) c = u8('a' + rng() % sigma);
            ps.add(p);
        }
        std::vector<u8> text(rng() % 300);
        for (auto& c : text) c = u8('a' + rng() % sigma);
        check_all(ps, text);
    }
}

TEST_CASE("automaton structure") {
    const auto ps = make_patterns({"he", "she", "his", "hers"});
    const Trie t = build_trie(ps);
    CHECK(t.num_states == 10);  // root + h,he,her,hers,hi,his + s,sh,she
    CHECK(t.max_len == 4);
    const DenseAC d = build_dense(t);
    const CompressedAC c = build_compressed(t);
    CHECK(c.bytes() < d.bytes());
    // "she" must also report "he"
    u32 st = 0;
    for (char ch : std::string("she")) st = d.step(st & kMask, u8(ch));
    CHECK((st & kFlag) != 0);
    CHECK(d.out.total[st & kMask] == 2);
}
