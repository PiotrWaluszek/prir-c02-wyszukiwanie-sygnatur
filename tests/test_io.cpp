#include <stdexcept>

#include <doctest/doctest.h>
#include "helpers.hpp"
#include "sigscan/cli.hpp"
#include "sigscan/patterns.hpp"

using namespace sigscan;

TEST_CASE("pattern file parsing") {
    const auto ps = parse_patterns("# comment\n4854 5450\n\nff00  # trailing\nAbCd\n");
    REQUIRE(ps.size() == 3);
    CHECK(ps.pats[0] == std::vector<u8>{0x48, 0x54, 0x54, 0x50});
    CHECK(ps.pats[1] == std::vector<u8>{0xff, 0x00});
    CHECK(ps.pats[2] == std::vector<u8>{0xab, 0xcd});
    CHECK(ps.min_len == 2);
    CHECK(ps.max_len == 4);
    CHECK(ps.total_len == 8);
}

TEST_CASE("pattern file errors carry the line number") {
    CHECK_THROWS_WITH_AS(parse_patterns("aa\nzz\n", "p.pat"), "p.pat:2: invalid hex character 'z'",
                         std::runtime_error);
    CHECK_THROWS_WITH_AS(parse_patterns("abc\n", "p.pat"), "p.pat:1: odd number of hex digits", std::runtime_error);
}

TEST_CASE("pattern serialization round-trip") {
    PatternSet ps;
    ps.add({1, 2, 3});
    ps.add({0});
    ps.add({0xff, 0xfe});
    const auto back = deserialize_patterns(serialize_patterns(ps));
    CHECK(back.pats == ps.pats);
    CHECK(back.max_len == 3);
}

TEST_CASE("size parsing") {
    CHECK(parse_size("123") == 123);
    CHECK(parse_size("4k") == 4096);
    CHECK(parse_size("1.5M") == 1572864);
    CHECK(parse_size("2GiB") == 2147483648ULL);
    CHECK_THROWS(parse_size("12x"));
}

TEST_CASE("checksum is order independent and position sensitive") {
    std::vector<Match> a{{1, 10}, {2, 20}, {1, 30}};
    std::vector<Match> b{{1, 30}, {1, 10}, {2, 20}};
    CHECK(summarize(a) == summarize(b));
    b[0].pos = 31;
    CHECK(!(summarize(a) == summarize(b)));
}
