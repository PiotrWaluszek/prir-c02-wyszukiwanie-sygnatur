// Test helpers: brute-force oracle and runners for every layout and split.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "sigscan/automaton.hpp"
#include "sigscan/scan.hpp"

namespace sigscan::test {

inline std::vector<u8> bytes(const std::string& s) { return {s.begin(), s.end()}; }

inline PatternSet make_patterns(const std::vector<std::string>& v) {
    PatternSet ps;
    for (const auto& s : v) ps.add(bytes(s));
    return ps;
}

inline std::vector<Match> sorted(std::vector<Match> m) {
    std::sort(m.begin(), m.end(), match_less);
    return m;
}

// slow but obviously correct
inline std::vector<Match> naive(const PatternSet& ps, const std::vector<u8>& text) {
    std::vector<Match> m;
    for (u64 i = 0; i < text.size(); ++i)
        for (u32 p = 0; p < ps.size(); ++p) {
            const auto& pat = ps.pats[p];
            if (i + pat.size() <= text.size() && std::equal(pat.begin(), pat.end(), text.begin() + i))
                m.push_back({p, i});
        }
    return sorted(m);
}

template <class AC>
std::vector<Match> scan_all(const AC& ac, const std::vector<u8>& text) {
    ListSink s;
    scan_chunk(ac, text.data(), text.size(), text.size(), 0, s);
    return sorted(s.matches);
}

template <class AC>
Summary count_all(const AC& ac, const std::vector<u8>& text) {
    CountSink s;
    scan_chunk(ac, text.data(), text.size(), text.size(), 0, s);
    return s.summary();
}

// fixed-size chunks with the ownership rule, like OpenMP/MPI
template <class AC>
std::vector<Match> scan_chunked(const AC& ac, const std::vector<u8>& text, u64 chunk) {
    ListSink s;
    const u64 n = text.size();
    for (u64 b = 0; b < n; b += chunk) scan_chunk(ac, text.data() + b, n - b, std::min(chunk, n - b), b, s);
    return sorted(s.matches);
}

template <class AC>
Summary count_chunked(const AC& ac, const std::vector<u8>& text, u64 chunk) {
    CountSink s;
    const u64 n = text.size();
    for (u64 b = 0; b < n; b += chunk) scan_chunk(ac, text.data() + b, n - b, std::min(chunk, n - b), b, s);
    return s.summary();
}

// chunks scanned with K interleaved lanes
template <class AC, class Sink>
void scan_lanes_chunked(const AC& ac, const std::vector<u8>& text, u64 chunk, int lanes, Sink& s) {
    const u64 n = text.size();
    for (u64 b = 0; b < n; b += chunk) scan_lanes(lanes, ac, text.data() + b, n - b, std::min(chunk, n - b), b, s);
}

inline std::vector<Match> pfac_all(const PfacTable& pf, const std::vector<u8>& text) {
    ListSink s;
    pfac_scan_cpu(pf, text.data(), text.size(), text.size(), 0, s);
    return sorted(s.matches);
}

}  // namespace sigscan::test
