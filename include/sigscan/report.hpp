// Code shared by the programs: options, loading inputs, choosing the layout, CSV output.
#pragma once

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include "sigscan/automaton.hpp"
#include "sigscan/cli.hpp"
#include "sigscan/io.hpp"
#include "sigscan/patterns.hpp"

namespace sigscan {

// One CSV line per repetition. Dataset parameters are in <dataset>.meta.json.
struct Record {
    std::string impl, variant = "-", layout, mode, dataset;
    int threads = 1, procs = 1, block = 0;
    u64 chunk = 0, text_bytes = 0, npat = 0, minlen = 0, maxlen = 0, automaton_bytes = 0, overlap_bytes = 0;
    int rep = 0;
    double io_ms = 0, build_ms = 0, h2d_ms = 0, search_ms = 0, d2h_ms = 0, merge_ms = 0, comm_ms = 0, total_ms = 0;
    Summary result;
    std::string extra;  // extra key=value pairs, e.g. MPI phases
};

std::string csv_header();
std::string to_csv(const Record& r);
std::string hostname();

// --text --patterns --mode --layout --out --reps --warmup --csv --csv-header --quiet
void add_common_options(Cli& cli, const std::string& default_layout = "dense");

struct Inputs {
    std::vector<u8> text;
    PatternSet pats;
    std::string dataset;  // file stem of --text
    double io_ms = 0;
};

Inputs load_inputs(const Cli& cli);

// Fills the dataset and size fields.
Record base_record(const Cli& cli, const Inputs& in, const std::string& impl);

// CSV on stdout with --csv, short summary on stderr.
void emit(const Cli& cli, const Record& r);

// Builds the chosen layout (timed) and calls f with it.
template <class F>
void with_automaton(const std::string& layout, const PatternSet& ps, double& build_ms, u64& bytes, F&& f) {
    Timer t;
    Trie trie = build_trie(ps);
    if (layout == "dense") {
        DenseAC ac = build_dense(trie);
        build_ms = t.ms();
        bytes = ac.bytes();
        f(ac);
    } else if (layout == "compressed") {
        CompressedAC ac = build_compressed(trie);
        build_ms = t.ms();
        bytes = ac.bytes();
        f(ac);
    } else {
        throw std::runtime_error("unknown --layout '" + layout + "' (dense|compressed)");
    }
}

}  // namespace sigscan
