// Scan loops shared by seq, OpenMP and MPI.
// Ownership: a chunk reports matches starting in [0, own_end) and reads up to Lmax - 1 bytes
// past own_end only to finish them. Starting from the root needs no warm-up.
#pragma once

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "sigscan/automaton.hpp"

namespace sigscan {

struct CountSink {
    u64 count = 0;
    u64 checksum = 0;

    // all patterns at st ending at end: H * (start + K) = H * (end + 1 + K) - H * len
    void on_state(const Outputs& o, u32 st, u64 end) {
        count += o.total[st];
        checksum += (end + 1 + kChecksumK) * o.sum_h[st] - o.sum_hl[st];
    }
    // only patterns ending exactly at st
    void on_own(const Outputs& o, u32 st, u64 start) {
        count += o.own_count(st);
        checksum += (start + kChecksumK) * o.own_h[st];
    }
    Summary summary() const { return {count, checksum}; }
};

struct ListSink {
    std::vector<Match> matches;

    void on_state(const Outputs& o, u32 st, u64 end) {
        for (u32 t = o.has_own(st) ? st : o.out_link[st]; t != kNone; t = o.out_link[t])
            on_own(o, t, end + 1 - o.depth[t]);
    }
    void on_own(const Outputs& o, u32 st, u64 start) {
        for (u32 k = o.own_begin[st]; k < o.own_begin[st + 1]; ++k) matches.push_back({o.own_ids[k], start});
    }
};

// Overlap part of a chunk: continues from state st at own_end.
template <class AC, class Sink>
u64 finish_overlap(const AC& ac, const u8* buf, u64 buf_len, u64 own_end, u64 base, Sink& sink, u32 st) {
    const Outputs& o = ac.out;
    const u64 lim = std::min<u64>(buf_len, own_end + (ac.max_len ? ac.max_len - 1 : 0));
    for (u64 i = own_end; i < lim; ++i) {
        st = ac.step(st & kMask, buf[i]);
        const u32 s = st & kMask;
        const u64 past = i - own_end + 1;
        if (o.depth[s] <= past) return past;
        if (st & kFlag) {
            for (u32 t = o.has_own(s) ? s : o.out_link[s]; t != kNone && o.depth[t] > past; t = o.out_link[t])
                sink.on_own(o, t, base + i + 1 - o.depth[t]);
        }
    }
    return lim - own_end;
}

// Scans one chunk (positions shifted by base), returns how many overlap bytes were read.
template <class AC, class Sink>
u64 scan_chunk(const AC& ac, const u8* buf, u64 buf_len, u64 own_end, u64 base, Sink& sink) {
    const Outputs& o = ac.out;
    own_end = std::min(own_end, buf_len);
    u32 st = 0;
    for (u64 i = 0; i < own_end; ++i) {
        st = ac.step(st & kMask, buf[i]);
        if (st & kFlag) [[unlikely]]
            sink.on_state(o, st & kMask, base + i);
    }
    return finish_overlap(ac, buf, buf_len, own_end, base, sink, st);
}

// Interleaving: the chunk is split into K lanes stepped together, so their memory loads overlap.
template <int K, class AC, class Sink>
u64 scan_interleaved(const AC& ac, const u8* buf, u64 buf_len, u64 own_end, u64 base, Sink& sink) {
    const Outputs& o = ac.out;
    own_end = std::min(own_end, buf_len);
    const u64 L = own_end / K;
    if (K == 1 || L < ac.max_len) return scan_chunk(ac, buf, buf_len, own_end, base, sink);
    u32 st[K] = {};
    for (u64 i = 0; i < L; ++i) {
        for (int j = 0; j < K; ++j) {
            st[j] = ac.step(st[j] & kMask, buf[j * L + i]);
            if (st[j] & kFlag) [[unlikely]]
                sink.on_state(o, st[j] & kMask, base + j * L + i);
        }
    }
    // last lane also takes the remainder
    u32 last = st[K - 1];
    for (u64 i = K * L; i < own_end; ++i) {
        last = ac.step(last & kMask, buf[i]);
        if (last & kFlag) [[unlikely]]
            sink.on_state(o, last & kMask, base + i);
    }
    u64 overlap = 0;
    for (int j = 0; j + 1 < K; ++j)
        overlap += finish_overlap(ac, buf + j * L, buf_len - j * L, L, base + j * L, sink, st[j]);
    const u64 off = (K - 1) * L;
    return overlap + finish_overlap(ac, buf + off, buf_len - off, own_end - off, base + off, sink, last);
}

template <class AC, class Sink>
u64 scan_lanes(int lanes, const AC& ac, const u8* buf, u64 buf_len, u64 own_end, u64 base, Sink& sink) {
    switch (lanes) {
        case 1: return scan_chunk(ac, buf, buf_len, own_end, base, sink);
        case 2: return scan_interleaved<2>(ac, buf, buf_len, own_end, base, sink);
        case 4: return scan_interleaved<4>(ac, buf, buf_len, own_end, base, sink);
        case 8: return scan_interleaved<8>(ac, buf, buf_len, own_end, base, sink);
        case 16: return scan_interleaved<16>(ac, buf, buf_len, own_end, base, sink);
        default: throw std::invalid_argument("--interleave must be 1, 2, 4, 8 or 16");
    }
}

// PFAC on the CPU, for checking the GPU table and counting partial matches.
template <class Sink>
void pfac_scan_cpu(const PfacTable& t, const u8* buf, u64 buf_len, u64 own_end, u64 base, Sink& sink) {
    own_end = std::min(own_end, buf_len);
    for (u64 i = 0; i < own_end; ++i) {
        u32 st = 0;
        for (u64 j = i; j < buf_len; ++j) {
            const u32 nx = t.next[(std::size_t(st) << 8) | buf[j]];
            if (nx == 0) break;
            st = nx & kMask;
            if (nx & kFlag) sink.on_own(t.out, st, base + i);
        }
    }
}

// Partial-match statistics.
struct WorkStats {
    u64 starts = 0;          // start positions examined
    u64 pfac_steps = 0;      // transitions over all PFAC walks
    u64 pfac_nonzero = 0;    // walks with at least one step
    u64 pfac_max = 0;        // longest walk
    u64 ac_depth_sum = 0;    // sum of AC state depth after each byte
    double pfac_mean() const { return starts ? double(pfac_steps) / double(starts) : 0.0; }
    double pfac_partial_ratio() const { return starts ? double(pfac_nonzero) / double(starts) : 0.0; }
    double ac_depth_mean() const { return starts ? double(ac_depth_sum) / double(starts) : 0.0; }
};

WorkStats measure_work(const PfacTable& pf, const DenseAC& ac, const u8* text, u64 n);

}  // namespace sigscan
