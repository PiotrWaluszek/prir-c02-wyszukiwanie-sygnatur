// sigscan-seq - sequential Aho-Corasick, the reference for all other versions.
#include <cinttypes>
#include <cstdio>
#include <exception>
#include <type_traits>

#include "sigscan/report.hpp"
#include "sigscan/scan.hpp"

using namespace sigscan;

namespace {

// --stats: automaton sizes and partial-match work as one CSV line
int print_stats(const Inputs& in) {
    const Trie trie = build_trie(in.pats);
    const DenseAC dense = build_dense(trie);
    const CompressedAC comp = build_compressed(trie);
    const PfacTable pf = build_pfac(trie);
    const WorkStats ws = measure_work(pf, dense, in.text.data(), in.text.size());
    std::printf("dataset,text_bytes,npat,states,dense_bytes,compressed_bytes,pfac_table_bytes,"
                "pfac_mean_steps,pfac_partial_ratio,pfac_max_steps,ac_depth_mean\n");
    std::printf("%s,%zu,%zu,%u,%zu,%zu,%zu,%.6f,%.6f,%" PRIu64 ",%.6f\n", in.dataset.c_str(), in.text.size(),
                in.pats.size(), trie.num_states, dense.bytes(), comp.bytes(), pf.table_bytes(), ws.pfac_mean(),
                ws.pfac_partial_ratio(), ws.pfac_max, ws.ac_depth_mean());
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
    Cli cli("sigscan-seq", "sequential Aho-Corasick signature search (reference)");
    add_common_options(cli);
    cli.opt("algo", "ac", "ac = Aho-Corasick scan; pfac = CPU emulation of the GPU PFAC algorithm (validation)")
        .opt("interleave", "1", "lanes advanced in lockstep by the scan loop: 1, 2, 4, 8, 16 (memory-level parallelism)")
        .flag("stats", "print automaton sizes and partial-match statistics as CSV and exit");
    cli.parse(argc, argv);

    const Inputs in = load_inputs(cli);
    if (cli.on("stats")) return print_stats(in);

    const bool list = cli.str("mode") == "list";
    const bool pfac = cli.str("algo") == "pfac";
    const int lanes = int(cli.integer("interleave"));
    const int reps = int(cli.integer("reps")), warmup = int(cli.integer("warmup"));
    const u8* text = in.text.data();
    const u64 n = in.text.size();
    std::vector<Match> last;

    if (cli.on("csv-header")) std::printf("%s\n", csv_header().c_str());
    for (int rep = -warmup; rep < reps; ++rep) {
        Record r = base_record(cli, in, "seq");
        r.rep = rep;
        if (lanes > 1) r.variant = "il" + std::to_string(lanes);
        auto run = [&](const auto& ac) {
            Timer t;
            if (list) {
                ListSink s;
                if constexpr (std::is_same_v<std::decay_t<decltype(ac)>, PfacTable>)
                    pfac_scan_cpu(ac, text, n, n, 0, s);
                else
                    scan_lanes(lanes, ac, text, n, n, 0, s);
                r.search_ms = t.ms();
                r.result = summarize(s.matches);
                last = std::move(s.matches);
            } else {
                CountSink s;
                if constexpr (std::is_same_v<std::decay_t<decltype(ac)>, PfacTable>)
                    pfac_scan_cpu(ac, text, n, n, 0, s);
                else
                    scan_lanes(lanes, ac, text, n, n, 0, s);
                r.search_ms = t.ms();
                r.result = s.summary();
            }
        };
        if (pfac) {
            r.variant = "pfac-cpu";
            r.layout = "pfac";
            Timer t;
            const PfacTable pf = build_pfac(build_trie(in.pats));
            r.build_ms = t.ms();
            r.automaton_bytes = pf.table_bytes() + pf.out.bytes();
            run(pf);
        } else {
            with_automaton(cli.str("layout"), in.pats, r.build_ms, r.automaton_bytes, run);
        }
        r.total_ms = r.io_ms + r.build_ms + r.search_ms;
        if (rep >= 0) emit(cli, r);
    }
    if (list && !cli.str("out").empty()) write_matches(cli.str("out"), last);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "sigscan-seq: %s\n", e.what());
    return 1;
}
