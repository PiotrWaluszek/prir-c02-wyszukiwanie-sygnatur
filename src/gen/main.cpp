// sigscan-gen - generates text, patterns and ground truth.
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>

#include "sigscan/cli.hpp"
#include "sigscan/generator.hpp"
#include "sigscan/io.hpp"

using namespace sigscan;

int main(int argc, char** argv) try {
    Cli cli("sigscan-gen", "generate a byte stream, a pattern set and the ground truth of injected matches");
    cli.opt("out", "", "output prefix: writes PREFIX.bin, .pat, .truth.csv, .meta.json")
        .opt("size", "64M", "text size (suffixes k/M/G = KiB/MiB/GiB)")
        .opt("npat", "1000", "number of patterns")
        .opt("minlen", "8", "minimum pattern length [B]")
        .opt("maxlen", "32", "maximum pattern length [B]")
        .opt("hit-rate", "10", "injected full occurrences per MiB")
        .opt("partial-rate", "0", "injected incomplete pattern prefixes per MiB (partial matches)")
        .opt("prefix-share", "0", "fraction of patterns starting with a shared prefix (0..1)")
        .opt("prefix-len", "4", "length of shared prefixes [B]")
        .opt("prefix-groups", "8", "number of distinct shared prefixes")
        .opt("alphabet", "full", "full (256) | ascii (95 printable) | dna (ACGT) | N (bytes 0..N-1)")
        .opt("boundary-block", "0", "if > 0, also place one occurrence across every multiple of this size")
        .opt("hot-region", "", "start:len:mult - hit density multiplied by mult inside the region (sizes ok)")
        .flag("no-match", "text and patterns disjoint by construction (zero matches)")
        .opt("seed", "2026", "master RNG seed");
    cli.parse(argc, argv);
    if (cli.str("out").empty()) throw std::runtime_error("--out is required");

    GenParams p;
    p.size = cli.size("size");
    p.npat = u32(cli.integer("npat"));
    p.minlen = u32(cli.integer("minlen"));
    p.maxlen = u32(cli.integer("maxlen"));
    p.hit_rate = cli.real("hit-rate");
    p.partial_rate = cli.real("partial-rate");
    p.prefix_share = cli.real("prefix-share");
    p.prefix_len = u32(cli.integer("prefix-len"));
    p.prefix_groups = u32(cli.integer("prefix-groups"));
    p.alphabet = cli.str("alphabet");
    p.boundary_block = cli.size("boundary-block");
    p.no_match = cli.on("no-match");
    p.seed = u64(std::stoull(cli.str("seed")));
    if (const std::string hr = cli.str("hot-region"); !hr.empty()) {
        const auto a = hr.find(':'), b = hr.find(':', a + 1);
        if (a == std::string::npos || b == std::string::npos) throw std::runtime_error("--hot-region start:len:mult");
        p.hot_start = parse_size(hr.substr(0, a));
        p.hot_len = parse_size(hr.substr(a + 1, b - a - 1));
        p.hot_mult = std::stod(hr.substr(b + 1));
    }

    Timer t;
    GenOutput g = generate(p);
    const double gen_ms = t.ms();
    const std::string out = cli.str("out");
    if (const auto dir = std::filesystem::path(out).parent_path(); !dir.empty()) std::filesystem::create_directories(dir);
    write_file(out + ".bin", g.text.data(), g.text.size());
    save_patterns(out + ".pat", g.pats);
    write_matches(out + ".truth.csv", g.truth);
    std::ofstream(out + ".meta.json") << meta_json(p, g);
    std::fprintf(stderr,
                 "sigscan-gen: %s.bin %llu B, %zu patterns (len %zu..%zu), injected %llu hits + %llu boundary + "
                 "%llu partial, dropped %llu, %.0f ms\n",
                 out.c_str(), (unsigned long long)p.size, g.pats.size(), g.pats.min_len, g.pats.max_len,
                 (unsigned long long)g.injected_hits, (unsigned long long)g.injected_boundary,
                 (unsigned long long)g.injected_partial, (unsigned long long)g.dropped, gen_ms);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "sigscan-gen: %s\n", e.what());
    return 1;
}
