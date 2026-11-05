// sigscan-omp - chunks with an Lmax - 1 overlap, a chunk owns matches starting in it.
// blocks = one block per thread, static/dynamic/guided = chunks of --chunk bytes.
#include <omp.h>

#include <algorithm>
#include <cstdio>
#include <exception>

#include "sigscan/report.hpp"
#include "sigscan/scan.hpp"

using namespace sigscan;

namespace {

struct alignas(64) Slot {  // per thread, aligned against false sharing
    CountSink count;
    ListSink list;
    u64 overlap = 0;
};

}  // namespace

int main(int argc, char** argv) try {
    Cli cli("sigscan-omp", "OpenMP Aho-Corasick signature search");
    add_common_options(cli);
    cli.opt("threads", std::to_string(omp_get_max_threads()), "number of OpenMP threads")
        .opt("schedule", "blocks", "blocks (one block per thread) | static | dynamic | guided (chunks of --chunk)")
        .opt("chunk", "1M", "chunk size for static/dynamic/guided (suffixes k/M/G)")
        .opt("interleave", "1", "lanes per chunk advanced in lockstep: 1, 2, 4, 8, 16");
    cli.parse(argc, argv);

    const Inputs in = load_inputs(cli);
    const bool list = cli.str("mode") == "list";
    const int p = int(cli.integer("threads"));
    const std::string sched = cli.str("schedule");
    const int lanes = int(cli.integer("interleave"));
    const u64 n = in.text.size();
    const u8* text = in.text.data();

    u64 chunk = 0, nchunks = 0;
    if (sched == "blocks") {
        nchunks = u64(p);
    } else {
        chunk = std::max<u64>(1, cli.size("chunk"));
        nchunks = std::max<u64>(1, (n + chunk - 1) / chunk);
        if (sched != "static" && sched != "dynamic" && sched != "guided")
            throw std::runtime_error("unknown --schedule '" + sched + "'");
    }
#if _OPENMP >= 200805  // OpenMP 3.0+
    if (sched == "dynamic") omp_set_schedule(omp_sched_dynamic, 1);
    else if (sched == "guided") omp_set_schedule(omp_sched_guided, 1);
    else omp_set_schedule(omp_sched_static, 1);  // blocks, static
#endif
    // Chunk k owns start positions [begin(k), begin(k+1)).
    auto begin = [&](u64 k) { return chunk ? std::min(n, k * chunk) : (n * k) / nchunks; };

    const int reps = int(cli.integer("reps")), warmup = int(cli.integer("warmup"));
    std::vector<Match> last;
    if (cli.on("csv-header")) std::printf("%s\n", csv_header().c_str());
    for (int rep = -warmup; rep < reps; ++rep) {
        Record r = base_record(cli, in, "omp");
        r.rep = rep;
        r.variant = lanes > 1 ? sched + "+il" + std::to_string(lanes) : sched;
        r.threads = p;
        r.chunk = chunk ? chunk : (n + nchunks - 1) / nchunks;
        with_automaton(cli.str("layout"), in.pats, r.build_ms, r.automaton_bytes, [&](const auto& ac) {
            std::vector<Slot> slots(p);
            Timer t;
#pragma omp parallel num_threads(p)
            {
                Slot& my = slots[omp_get_thread_num()];
#define SIGSCAN_CHUNK_LOOP                                                                         \
    for (long long k = 0; k < (long long)nchunks; ++k) {                                           \
        const u64 s = begin(u64(k)), e = begin(u64(k) + 1);                                        \
        if (s == e) continue;                                                                      \
        my.overlap += list ? scan_lanes(lanes, ac, text + s, n - s, e - s, s, my.list)             \
                           : scan_lanes(lanes, ac, text + s, n - s, e - s, s, my.count);           \
    }
#if _OPENMP >= 200805
#pragma omp for schedule(runtime)
                SIGSCAN_CHUNK_LOOP
#else  // MSVC: OpenMP 2.0, no omp_set_schedule
                if (sched == "dynamic") {
#pragma omp for schedule(dynamic, 1)
                    SIGSCAN_CHUNK_LOOP
                } else if (sched == "guided") {
#pragma omp for schedule(guided, 1)
                    SIGSCAN_CHUNK_LOOP
                } else {  // blocks, static
#pragma omp for schedule(static, 1)
                    SIGSCAN_CHUNK_LOOP
                }
#endif
#undef SIGSCAN_CHUNK_LOOP
            }
            r.search_ms = t.ms();
            Timer tm;
            Summary sum;
            std::vector<Match> all;
            for (Slot& sl : slots) {
                r.overlap_bytes += sl.overlap;
                if (list) all.insert(all.end(), sl.list.matches.begin(), sl.list.matches.end());
                else sum.add(sl.count.summary());
            }
            r.merge_ms = tm.ms();
            if (list) {
                r.result = summarize(all);
                last = std::move(all);
            } else {
                r.result = sum;
            }
        });
        r.total_ms = r.io_ms + r.build_ms + r.search_ms + r.merge_ms;
        if (rep >= 0) emit(cli, r);
    }
    if (list && !cli.str("out").empty()) write_matches(cli.str("out"), last);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "sigscan-omp: %s\n", e.what());
    return 1;
}
