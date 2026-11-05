#include "sigscan/io.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace sigscan {

namespace {
struct FileCloser {
    void operator()(std::FILE* f) const { std::fclose(f); }
};
using File = std::unique_ptr<std::FILE, FileCloser>;

File open_or_throw(const std::string& path, const char* mode) {
    File f(std::fopen(path.c_str(), mode));
    if (!f) throw std::runtime_error("cannot open file: " + path);
    return f;
}
}  // namespace

Summary summarize(const std::vector<Match>& m) {
    Summary s;
    for (const auto& x : m) {
        ++s.count;
        s.checksum += match_term(x.pid, x.pos);
    }
    return s;
}

u64 file_size(const std::string& path) {
    // struct stat has a 32-bit size on Windows
    std::error_code ec;
    const auto n = std::filesystem::file_size(path, ec);
    if (ec) throw std::runtime_error("cannot stat file: " + path);
    return u64(n);
}

std::vector<u8> read_file(const std::string& path) {
    const u64 n = file_size(path);
    std::vector<u8> buf(n);
    File f = open_or_throw(path, "rb");
    if (n && std::fread(buf.data(), 1, n, f.get()) != n) throw std::runtime_error("short read: " + path);
    return buf;
}

void write_file(const std::string& path, const u8* data, std::size_t n) {
    File f = open_or_throw(path, "wb");
    if (n && std::fwrite(data, 1, n, f.get()) != n) throw std::runtime_error("short write: " + path);
}

void write_matches(const std::string& path, std::vector<Match>& m) {
    std::sort(m.begin(), m.end(), match_less);
    File f = open_or_throw(path, "wb");
    std::setvbuf(f.get(), nullptr, _IOFBF, 1 << 20);
    std::fputs("pattern_id,offset\n", f.get());
    for (const auto& x : m) std::fprintf(f.get(), "%u,%llu\n", x.pid, (unsigned long long)x.pos);
}

std::vector<Match> read_matches(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open matches file: " + path);
    std::vector<Match> m;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto comma = line.find(',');
        m.push_back({u32(std::stoul(line.substr(0, comma))), u64(std::stoull(line.substr(comma + 1)))});
    }
    return m;
}

}  // namespace sigscan
