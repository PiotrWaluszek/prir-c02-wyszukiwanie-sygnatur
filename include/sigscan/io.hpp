// File helpers: raw bytes, match CSV, small JSON writer.
#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "sigscan/common.hpp"

namespace sigscan {

std::vector<u8> read_file(const std::string& path);
void write_file(const std::string& path, const u8* data, std::size_t n);
u64 file_size(const std::string& path);

// CSV "pattern_id,offset", sorted by offset.
void write_matches(const std::string& path, std::vector<Match>& m);
std::vector<Match> read_matches(const std::string& path);

class Timer {
public:
    Timer() : t0_(clock::now()) {}
    void reset() { t0_ = clock::now(); }
    double ms() const { return std::chrono::duration<double, std::milli>(clock::now() - t0_).count(); }

private:
    using clock = std::chrono::steady_clock;
    clock::time_point t0_;
};

}  // namespace sigscan
