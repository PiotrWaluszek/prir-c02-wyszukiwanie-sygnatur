#include "sigscan/patterns.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace sigscan {

void PatternSet::add(std::vector<u8> p) {
    if (p.empty()) throw std::runtime_error("empty pattern");
    min_len = pats.empty() ? p.size() : std::min(min_len, p.size());
    max_len = std::max(max_len, p.size());
    total_len += p.size();
    pats.push_back(std::move(p));
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

PatternSet parse_patterns(const std::string& text, const std::string& origin) {
    PatternSet ps;
    std::istringstream in(text);
    std::string line;
    std::size_t lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        auto fail = [&](const std::string& why) {
            throw std::runtime_error(origin + ":" + std::to_string(lineno) + ": " + why);
        };
        if (auto h = line.find('#'); h != std::string::npos) line.erase(h);
        std::vector<u8> bytes;
        int hi = -1;
        for (char c : line) {
            if (c == ' ' || c == '\t' || c == '\r') continue;
            const int v = hex_value(c);
            if (v < 0) fail(std::string("invalid hex character '") + c + "'");
            if (hi < 0) {
                hi = v;
            } else {
                bytes.push_back(u8(hi << 4 | v));
                hi = -1;
            }
        }
        if (hi >= 0) fail("odd number of hex digits");
        if (!bytes.empty()) ps.add(std::move(bytes));
    }
    return ps;
}

PatternSet load_patterns(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open patterns file: " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return parse_patterns(ss.str(), path);
}

std::string to_hex(const std::vector<u8>& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    s.reserve(bytes.size() * 2);
    for (u8 b : bytes) {
        s.push_back(digits[b >> 4]);
        s.push_back(digits[b & 15]);
    }
    return s;
}

void save_patterns(const std::string& path, const PatternSet& ps) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot write patterns file: " + path);
    for (const auto& p : ps.pats) f << to_hex(p) << '\n';
}

std::vector<u8> serialize_patterns(const PatternSet& ps) {
    std::vector<u8> buf(4 + 4 * ps.size() + ps.total_len);
    auto put32 = [&](std::size_t at, u32 v) { std::memcpy(buf.data() + at, &v, 4); };
    put32(0, u32(ps.size()));
    std::size_t at = 4 + 4 * ps.size();
    for (std::size_t i = 0; i < ps.size(); ++i) {
        put32(4 + 4 * i, u32(ps.pats[i].size()));
        std::memcpy(buf.data() + at, ps.pats[i].data(), ps.pats[i].size());
        at += ps.pats[i].size();
    }
    return buf;
}

PatternSet deserialize_patterns(const std::vector<u8>& buf) {
    auto get32 = [&](std::size_t at) {
        u32 v;
        std::memcpy(&v, buf.data() + at, 4);
        return v;
    };
    PatternSet ps;
    const u32 n = get32(0);
    std::size_t at = 4 + 4 * std::size_t(n);
    for (u32 i = 0; i < n; ++i) {
        const u32 len = get32(4 + 4 * std::size_t(i));
        ps.add(std::vector<u8>(buf.begin() + at, buf.begin() + at + len));
        at += len;
    }
    return ps;
}

}  // namespace sigscan
