// sigscan-mpi - rank 0 reads the inputs, patterns are broadcast, the text is scattered and every
// rank gets the Lmax - 1 bytes after its part (halo). Results are reduced (count) or gathered (list).
// Barriers around each phase, so a phase takes as long as the slowest rank.
#include <mpi.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <exception>

#include "sigscan/report.hpp"
#include "sigscan/scan.hpp"

using namespace sigscan;

namespace {

int rank_ = 0, nprocs_ = 1;

int to_int(u64 v) {
    if (v > u64(INT_MAX)) throw std::runtime_error("message larger than INT_MAX bytes: use a smaller text");
    return int(v);
}

struct PhaseTimer {
    double t0 = 0;
    void start() {
        MPI_Barrier(MPI_COMM_WORLD);
        t0 = MPI_Wtime();
    }
    double stop_ms() {
        MPI_Barrier(MPI_COMM_WORLD);
        return (MPI_Wtime() - t0) * 1e3;
    }
};

void bcast_bytes(std::vector<u8>& buf) {
    u64 size = buf.size();
    MPI_Bcast(&size, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
    buf.resize(size);
    MPI_Bcast(buf.data(), to_int(size), MPI_BYTE, 0, MPI_COMM_WORLD);
}

}  // namespace

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank_);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs_);
    int rc = 0;
    try {
        Cli cli("sigscan-mpi", "MPI Aho-Corasick signature search (run with mpirun -np P)");
        add_common_options(cli);
        cli.parse(argc, argv);
        const bool list = cli.str("mode") == "list";
        const int P = nprocs_;

        // only rank 0 has the inputs, the others need n for the split
        Inputs in;
        std::vector<u8> pat_buf;
        if (rank_ == 0) {
            in = load_inputs(cli);
            pat_buf = serialize_patterns(in.pats);
        }
        u64 n = in.text.size();
        MPI_Bcast(&n, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);
        // floor(n*r/P) without 128-bit math
        auto begin = [&](int r) { return n / u64(P) * u64(r) + n % u64(P) * u64(r) / u64(P); };
        const u64 my_s = begin(rank_), my_e = begin(rank_ + 1);

        const int reps = int(cli.integer("reps")), warmup = int(cli.integer("warmup"));
        std::vector<Match> last;
        if (rank_ == 0 && cli.on("csv-header")) std::printf("%s\n", csv_header().c_str());

        for (int rep = -warmup; rep < reps; ++rep) {
            PhaseTimer pt;
            // 1. patterns
            pt.start();
            std::vector<u8> pb = rank_ == 0 ? pat_buf : std::vector<u8>{};
            bcast_bytes(pb);
            const double bcast_ms = pt.stop_ms();
            const PatternSet ps = deserialize_patterns(pb);
            const u64 H = ps.max_len ? ps.max_len - 1 : 0;  // overlap length
            const u64 my_h = std::min(H, n - my_e);         // overlap bytes this rank needs

            // 2. text: disjoint parts with Scatterv
            pt.start();
            std::vector<u8> local(my_e - my_s + my_h);
            {
                std::vector<int> counts(P), displs(P);
                for (int r = 0; r < P; ++r) {
                    counts[r] = to_int(begin(r + 1) - begin(r));
                    displs[r] = to_int(begin(r));
                }
                MPI_Scatterv(rank_ == 0 ? in.text.data() : nullptr, counts.data(), displs.data(), MPI_BYTE,
                             local.data(), counts[rank_], MPI_BYTE, 0, MPI_COMM_WORLD);
            }
            const double scatter_ms = pt.stop_ms();

            // 3. halo, can span several ranks when parts are shorter than Lmax
            pt.start();
            {
                std::vector<int> scount(P, 0), sdispl(P, 0), rcount(P, 0), rdispl(P, 0);
                for (int q = 0; q < P; ++q) {
                    const u64 qe = begin(q + 1), qh = std::min(H, n - qe);
                    // what q needs from me: [qe, qe + qh) ∩ [my_s, my_e)
                    const u64 lo = std::max(qe, my_s), hi = std::min(qe + qh, my_e);
                    if (q != rank_ && lo < hi) {
                        scount[q] = to_int(hi - lo);
                        sdispl[q] = to_int(lo - my_s);
                    }
                    // what I need from q: [my_e, my_e + my_h) ∩ [q_s, q_e)
                    const u64 lo2 = std::max(my_e, begin(q)), hi2 = std::min(my_e + my_h, begin(q + 1));
                    if (q != rank_ && lo2 < hi2) {
                        rcount[q] = to_int(hi2 - lo2);
                        rdispl[q] = to_int(lo2 - my_e);
                    }
                }
                std::vector<u8> halo(my_h);
                MPI_Alltoallv(local.data(), scount.data(), sdispl.data(), MPI_BYTE, halo.data(), rcount.data(),
                              rdispl.data(), MPI_BYTE, MPI_COMM_WORLD);
                std::copy(halo.begin(), halo.end(), local.begin() + (my_e - my_s));
            }
            const double halo_ms = pt.stop_ms();

            // 4. automaton (each rank builds its own)
            Record r;
            if (rank_ == 0) r = base_record(cli, in, "mpi");
            r.rep = rep;
            r.variant = "scatter-halo";
            r.procs = P;
            r.chunk = (n + P - 1) / P;
            Summary local_sum;
            std::vector<Match> local_list;
            u64 overlap = 0;
            double build_ms = 0, scan_ms = 0;
            pt.start();
            with_automaton(cli.str("layout"), ps, build_ms, r.automaton_bytes, [&](const auto& ac) {
                build_ms = pt.stop_ms();
                // 5. scan
                pt.start();
                if (list) {
                    ListSink s;
                    overlap = scan_chunk(ac, local.data(), local.size(), my_e - my_s, my_s, s);
                    local_list = std::move(s.matches);
                } else {
                    CountSink s;
                    overlap = scan_chunk(ac, local.data(), local.size(), my_e - my_s, my_s, s);
                    local_sum = s.summary();
                }
                scan_ms = pt.stop_ms();
            });

            // 6. results
            pt.start();
            Summary total;
            std::vector<Match> all;
            if (list) {
                std::vector<u64> flat;
                flat.reserve(local_list.size() * 2);
                for (const auto& m : local_list) {
                    flat.push_back(m.pid);
                    flat.push_back(m.pos);
                }
                const int my_cnt = to_int(flat.size());
                std::vector<int> cnts(P), displs(P);
                MPI_Gather(&my_cnt, 1, MPI_INT, cnts.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
                std::vector<u64> recv;
                if (rank_ == 0) {
                    u64 acc = 0;
                    for (int q = 0; q < P; ++q) {
                        displs[q] = to_int(acc);
                        acc += u64(cnts[q]);
                    }
                    recv.resize(acc);
                }
                MPI_Gatherv(flat.data(), my_cnt, MPI_UINT64_T, recv.data(), cnts.data(), displs.data(),
                            MPI_UINT64_T, 0, MPI_COMM_WORLD);
                for (std::size_t k = 0; k + 1 < recv.size(); k += 2) all.push_back({u32(recv[k]), recv[k + 1]});
            } else {
                u64 mine[2] = {local_sum.count, local_sum.checksum}, sum[2] = {0, 0};
                MPI_Reduce(mine, sum, 2, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
                total = {sum[0], sum[1]};
            }
            const double gather_ms = pt.stop_ms();

            u64 overlap_sum = 0;
            MPI_Reduce(&overlap, &overlap_sum, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
            if (rank_ == 0) {
                if (list) {
                    total = summarize(all);
                    last = std::move(all);
                }
                r.result = total;
                r.overlap_bytes = overlap_sum;
                r.build_ms = build_ms;
                r.search_ms = scan_ms;
                r.comm_ms = bcast_ms + scatter_ms + halo_ms;
                r.merge_ms = gather_ms;
                r.total_ms = r.io_ms + r.comm_ms + r.build_ms + r.search_ms + r.merge_ms;
                char extra[160];
                std::snprintf(extra, sizeof extra, "bcast_ms=%.4f;scatter_ms=%.4f;halo_ms=%.4f;gather_ms=%.4f",
                              bcast_ms, scatter_ms, halo_ms, gather_ms);
                r.extra = extra;
                if (rep >= 0) emit(cli, r);
            }
        }
        if (rank_ == 0 && list && !cli.str("out").empty()) write_matches(cli.str("out"), last);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "sigscan-mpi[%d]: %s\n", rank_, e.what());
        rc = 1;
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    MPI_Finalize();
    return rc;
}
