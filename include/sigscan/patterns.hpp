// Pattern set, one hex string per line (*.pat).
#pragma once

#include <string>
#include <vector>

#include "sigscan/common.hpp"

namespace sigscan {

struct PatternSet {
    std::vector<std::vector<u8>> pats;  // index = pattern id
    std::size_t min_len = 0;
    std::size_t max_len = 0;
    std::size_t total_len = 0;

    std::size_t size() const { return pats.size(); }
    void add(std::vector<u8> p);
};

// Throws "file:line: reason" on bad input.
PatternSet load_patterns(const std::string& path);
PatternSet parse_patterns(const std::string& text, const std::string& origin = "<string>");
void save_patterns(const std::string& path, const PatternSet& ps);

std::string to_hex(const std::vector<u8>& bytes);

// Binary form for MPI_Bcast: [n][lengths][bytes]
std::vector<u8> serialize_patterns(const PatternSet& ps);
PatternSet deserialize_patterns(const std::vector<u8>& buf);

}  // namespace sigscan
