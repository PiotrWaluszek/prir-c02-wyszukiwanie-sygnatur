// sigscan-cuda - PFAC on the GPU. Thread i starts at position i and follows trie edges until
// the first missing one, so it only owns start position i.
// --kernel: global | ldg (read-only cache) | smem-text, smem-root (ldg + text tile or root row in
//           shared memory) | smem-all (ldg + both)
#include <cuda_runtime.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>

#include "sigscan/report.hpp"
#include "sigscan/scan.hpp"

using namespace sigscan;

#define CUDA_CHECK(x)                                                                                    \
    do {                                                                                                 \
        cudaError_t err_ = (x);                                                                          \
        if (err_ != cudaSuccess) throw std::runtime_error(std::string(#x) + ": " + cudaGetErrorString(err_)); \
    } while (0)

namespace {

constexpr unsigned kLdg = 1, kSmemText = 2, kSmemRoot = 4;

struct DevMatch {
    u64 pos;
    u32 pid;
    u32 pad;
};

struct DevTable {
    const u32* next;
    const u32* own_cnt;
    const u64* own_h;
    const u32* own_begin;
    const u32* own_ids;
};

template <unsigned F>
__device__ __forceinline__ u32 load_next(const u32* __restrict__ next, u32 idx) {
    if constexpr (F & kLdg) return __ldg(next + idx);
    else return next[idx];
}

// One PFAC walk from position i.
template <unsigned F, class OnOwn>
__device__ __forceinline__ void walk(const DevTable& t, const u32* root, const u8* tile, u64 tile_base, u64 tile_len,
                                     const u8* __restrict__ buf, u64 buf_len, u64 i, OnOwn&& on_own) {
    u32 st = 0;
    for (u64 j = i; j < buf_len; ++j) {
        u8 ch;
        if constexpr (F & kSmemText) ch = (j - tile_base < tile_len) ? tile[j - tile_base] : buf[j];
        else ch = buf[j];
        u32 nx;
        if constexpr (F & kSmemRoot) nx = st == 0 ? root[ch] : load_next<F>(t.next, st * 256u + ch);
        else nx = load_next<F>(t.next, st * 256u + ch);
        if (nx == 0) break;
        st = nx & kMask;
        if (nx & kFlag) on_own(st);
    }
}

__device__ __forceinline__ u64 warp_sum(u64 v) {
    for (int off = 16; off > 0; off >>= 1) v += __shfl_down_sync(0xffffffffu, v, off);
    return v;
}

// Block-wide sum of (a, b) added atomically to out[0], out[1].
__device__ void block_add(u64 a, u64 b, unsigned long long* out) {
    __shared__ u64 wa[32], wb[32];
    const int lane = threadIdx.x & 31, warp = threadIdx.x >> 5;
    a = warp_sum(a);
    b = warp_sum(b);
    if (lane == 0) {
        wa[warp] = a;
        wb[warp] = b;
    }
    __syncthreads();
    if (warp == 0) {
        const int nw = (blockDim.x + 31) >> 5;
        a = warp_sum(lane < nw ? wa[lane] : 0);
        b = warp_sum(lane < nw ? wb[lane] : 0);
        if (lane == 0) {
            atomicAdd(out, (unsigned long long)a);
            atomicAdd(out + 1, (unsigned long long)b);
        }
    }
}

template <unsigned F, bool kList>
__global__ void pfac_kernel(DevTable t, const u8* __restrict__ buf, u64 buf_len, u64 own_end, u64 base, u32 max_len,
                            unsigned long long* out_sum, DevMatch* out_list, unsigned long long* list_counter,
                            u64 list_capacity) {
    extern __shared__ unsigned char smem[];
    u32* root = reinterpret_cast<u32*>(smem);
    u8* tile = smem + ((F & kSmemRoot) ? 256 * sizeof(u32) : 0);
    if constexpr (F & kSmemRoot) {
        for (u32 k = threadIdx.x; k < 256; k += blockDim.x) root[k] = t.next[k];
        __syncthreads();
    }
    u64 cnt = 0, csum = 0;
    const u64 stride = u64(gridDim.x) * blockDim.x;
    for (u64 tile_base = u64(blockIdx.x) * blockDim.x; tile_base < own_end; tile_base += stride) {
        u64 tile_len = 0;
        if constexpr (F & kSmemText) {
            tile_len = min(u64(blockDim.x) + max_len - 1, buf_len - tile_base);
            __syncthreads();  // previous tile done
            for (u64 k = threadIdx.x; k < tile_len; k += blockDim.x) tile[k] = buf[tile_base + k];
            __syncthreads();
        }
        const u64 i = tile_base + threadIdx.x;
        if (i < own_end) {
            if constexpr (kList) {
                walk<F>(t, root, tile, tile_base, tile_len, buf, buf_len, i, [&](u32 st) {
                    for (u32 k = t.own_begin[st]; k < t.own_begin[st + 1]; ++k) {
                        const unsigned long long idx = atomicAdd(list_counter, 1ULL);
                        if (idx < list_capacity) out_list[idx] = DevMatch{base + i, t.own_ids[k], 0};
                    }
                });
            } else {
                u64 c = 0, h = 0;
                walk<F>(t, root, tile, tile_base, tile_len, buf, buf_len, i, [&](u32 st) {
                    c += t.own_cnt[st];
                    h += t.own_h[st];
                });
                cnt += c;
                csum += h * (base + i + kChecksumK);
            }
        }
    }
    if constexpr (!kList) block_add(cnt, csum, out_sum);
}

unsigned kernel_flags(const std::string& k) {
    if (k == "global") return 0;
    if (k == "ldg") return kLdg;
    if (k == "smem-text") return kLdg | kSmemText;
    if (k == "smem-root") return kLdg | kSmemRoot;
    if (k == "smem-all") return kLdg | kSmemText | kSmemRoot;
    throw std::runtime_error("unknown --kernel '" + k + "' (global|ldg|smem-text|smem-root|smem-all)");
}

struct Launch {
    unsigned flags;
    int block, grid;
    std::size_t smem;
    cudaStream_t stream;
};

template <bool kList>
void launch(const Launch& l, const DevTable& t, const u8* buf, u64 buf_len, u64 own_end, u64 base, u32 max_len,
            unsigned long long* out_sum, DevMatch* out_list = nullptr, unsigned long long* counter = nullptr,
            u64 capacity = 0) {
#define SIGSCAN_LAUNCH(F)                                                                                    \
    pfac_kernel<F, kList><<<l.grid, l.block, l.smem, l.stream>>>(t, buf, buf_len, own_end, base, max_len, out_sum, \
                                                                 out_list, counter, capacity)
    switch (l.flags) {
        case 0: SIGSCAN_LAUNCH(0); break;
        case kLdg: SIGSCAN_LAUNCH(kLdg); break;
        case kLdg | kSmemText: SIGSCAN_LAUNCH(kLdg | kSmemText); break;
        case kLdg | kSmemRoot: SIGSCAN_LAUNCH(kLdg | kSmemRoot); break;
        case kLdg | kSmemText | kSmemRoot: SIGSCAN_LAUNCH(kLdg | kSmemText | kSmemRoot); break;
        default: throw std::logic_error("bad kernel flags");
    }
#undef SIGSCAN_LAUNCH
    CUDA_CHECK(cudaGetLastError());
}

float elapsed(cudaEvent_t a, cudaEvent_t b) {
    float ms = 0;
    CUDA_CHECK(cudaEventElapsedTime(&ms, a, b));
    return ms;
}

}  // namespace

int main(int argc, char** argv) try {
    Cli cli("sigscan-cuda", "PFAC signature search on a CUDA GPU");
    add_common_options(cli, "pfac");
    cli.opt("kernel", "smem-all", "global | ldg | smem-text | smem-root | smem-all")
        .opt("block", "256", "threads per block")
        .opt("grid-mult", "32", "max resident blocks per SM used for the grid-stride grid")
        .opt("streams", "0", "0 = one transfer + one kernel; k > 0 = pipelined chunks over k CUDA streams")
        .opt("chunk", "32M", "chunk size for the pipelined mode")
        .opt("device", "0", "CUDA device index")
        .opt("warmup-ms", "2000", "keep the GPU busy this long before measuring (clocks ramp up from idle)")
        .flag("verify", "compare the count result with the CPU reference and fail on mismatch");
    cli.parse(argc, argv);

    const Inputs in = load_inputs(cli);
    const bool list = cli.str("mode") == "list";
    const std::string kname = cli.str("kernel");
    const unsigned flags = kernel_flags(kname);
    const int block = int(cli.integer("block"));
    const int streams = int(cli.integer("streams"));
    if (list && streams) throw std::runtime_error("list mode is supported only with --streams 0");
    const u64 n = in.text.size();

    CUDA_CHECK(cudaSetDevice(int(cli.integer("device"))));
    cudaDeviceProp prop{};
    CUDA_CHECK(cudaGetDeviceProperties(&prop, int(cli.integer("device"))));

    // automaton build on the host (timed like on the CPU)
    Timer tb;
    const PfacTable pf = build_pfac(build_trie(in.pats));
    const double build_ms = tb.ms();
    if (u64(pf.num_states) * 256 >= (1ULL << 32)) throw std::runtime_error("PFAC table too large for 32-bit indexing");
    const u32 max_len = u32(pf.max_len);
    const u64 H = max_len ? max_len - 1 : 0;
    std::vector<u32> own_cnt(pf.num_states);
    for (u32 s = 0; s < pf.num_states; ++s) own_cnt[s] = pf.out.own_count(s);

    Summary want{};
    if (cli.on("verify")) {
        CountSink s;
        scan_chunk(build_dense(build_trie(in.pats)), in.text.data(), n, n, 0, s);
        want = s.summary();
    }

    // pinned text for async copies, used in both modes
    u8* h_text = nullptr;
    CUDA_CHECK(cudaMallocHost(&h_text, std::max<u64>(1, n)));
    std::memcpy(h_text, in.text.data(), n);
    u8* d_text = nullptr;
    CUDA_CHECK(cudaMalloc(&d_text, std::max<u64>(1, n)));
    unsigned long long* d_sum = nullptr;
    CUDA_CHECK(cudaMalloc(&d_sum, 2 * sizeof(unsigned long long)));

    const std::size_t smem = ((flags & kSmemRoot) ? 256 * sizeof(u32) : 0) + ((flags & kSmemText) ? block + H : 0);
    if (smem > prop.sharedMemPerBlock) throw std::runtime_error("shared memory tile too large: lower --block");
    auto grid_for = [&](u64 positions) {
        const u64 want_blocks = (positions + block - 1) / block;
        return int(std::max<u64>(1, std::min<u64>(want_blocks, u64(prop.multiProcessorCount) * cli.integer("grid-mult"))));
    };
    std::vector<cudaStream_t> st(std::max(1, streams));
    for (auto& s : st) CUDA_CHECK(cudaStreamCreate(&s));
    cudaEvent_t e0, e1, e2, e3;
    for (auto* e : {&e0, &e1, &e2, &e3}) CUDA_CHECK(cudaEventCreate(e));

    // automaton on the device: PFAC table + outputs
    u32 *d_next = nullptr, *d_own_cnt = nullptr, *d_own_begin = nullptr, *d_own_ids = nullptr;
    u64* d_own_h = nullptr;
    auto alloc = [](auto** p, std::size_t count) {
        CUDA_CHECK(cudaMalloc(p, std::max<std::size_t>(1, count) * sizeof(**p)));
    };
    alloc(&d_next, pf.next.size());
    alloc(&d_own_cnt, own_cnt.size());
    alloc(&d_own_h, pf.out.own_h.size());
    if (list) {
        alloc(&d_own_begin, pf.out.own_begin.size());
        alloc(&d_own_ids, pf.out.own_ids.size());
    }
    auto upload = [](auto* dst, const auto& v) {
        if (!v.empty()) CUDA_CHECK(cudaMemcpy(dst, v.data(), v.size() * sizeof(v[0]), cudaMemcpyHostToDevice));
    };
    auto upload_table = [&] {
        upload(d_next, pf.next);
        upload(d_own_cnt, own_cnt);
        upload(d_own_h, pf.out.own_h);
        if (list) {
            upload(d_own_begin, pf.out.own_begin);
            upload(d_own_ids, pf.out.own_ids);
        }
    };
    const DevTable t{d_next, d_own_cnt, d_own_h, d_own_begin, d_own_ids};

    if (const double warm_ms = cli.real("warmup-ms"); warm_ms > 0) {
        upload_table();
        CUDA_CHECK(cudaMemcpy(d_text, h_text, n, cudaMemcpyHostToDevice));
        const Launch l{flags, block, grid_for(n), smem, nullptr};
        for (Timer tw; tw.ms() < warm_ms;) {
            launch<false>(l, t, d_text, n, n, 0, max_len, d_sum);
            CUDA_CHECK(cudaDeviceSynchronize());
        }
    }

    const int reps = int(cli.integer("reps")), warmup = int(cli.integer("warmup"));
    std::vector<Match> last;
    if (cli.on("csv-header")) std::printf("%s\n", csv_header().c_str());
    for (int rep = -warmup; rep < reps; ++rep) {
        Record r = base_record(cli, in, "cuda");
        r.rep = rep;
        r.variant = streams ? kname + "+pipeline" : kname;
        r.layout = "pfac";
        r.block = block;
        r.build_ms = build_ms;
        r.automaton_bytes = pf.table_bytes() + own_cnt.size() * 4 + pf.out.own_h.size() * 8;

        // automaton upload counts as H2D, buffers allocated once outside timing
        CUDA_CHECK(cudaMemset(d_sum, 0, 2 * sizeof(unsigned long long)));
        CUDA_CHECK(cudaEventRecord(e0));
        upload_table();
        unsigned long long h_sum[2] = {0, 0};
        int grid = 0;

        if (streams == 0) {
            CUDA_CHECK(cudaMemcpy(d_text, h_text, n, cudaMemcpyHostToDevice));
            CUDA_CHECK(cudaEventRecord(e1));
            grid = grid_for(n);
            const Launch l{flags, block, grid, smem, nullptr};
            launch<false>(l, t, d_text, n, n, 0, max_len, d_sum);
            CUDA_CHECK(cudaEventRecord(e2));
            CUDA_CHECK(cudaMemcpy(h_sum, d_sum, sizeof h_sum, cudaMemcpyDeviceToHost));
            if (list) {  // second pass with an exact-size buffer
                DevMatch* d_list = nullptr;
                unsigned long long* d_cnt = nullptr;
                CUDA_CHECK(cudaMalloc(&d_list, std::max<u64>(1, h_sum[0]) * sizeof(DevMatch)));
                CUDA_CHECK(cudaMalloc(&d_cnt, sizeof(unsigned long long)));
                CUDA_CHECK(cudaMemset(d_cnt, 0, sizeof(unsigned long long)));
                launch<true>(l, t, d_text, n, n, 0, max_len, nullptr, d_list, d_cnt, h_sum[0]);
                std::vector<DevMatch> hl(h_sum[0]);
                CUDA_CHECK(cudaMemcpy(hl.data(), d_list, hl.size() * sizeof(DevMatch), cudaMemcpyDeviceToHost));
                CUDA_CHECK(cudaFree(d_list));
                CUDA_CHECK(cudaFree(d_cnt));
                last.clear();
                for (const auto& m : hl) last.push_back({m.pid, m.pos});
            }
            CUDA_CHECK(cudaEventRecord(e3));
            CUDA_CHECK(cudaEventSynchronize(e3));
            r.h2d_ms = elapsed(e0, e1);
            r.search_ms = elapsed(e1, e2);
            r.d2h_ms = elapsed(e2, e3);
        } else {
            // pipeline: chunk c (with halo) goes to stream c % k
            CUDA_CHECK(cudaEventRecord(e1));
            const u64 chunk = std::max<u64>(1, cli.size("chunk"));
            r.chunk = chunk;
            grid = grid_for(std::min(chunk, n));
            u64 c = 0;
            for (u64 s = 0; s < n; s += chunk, ++c) {
                const u64 e = std::min(n, s + chunk), lim = std::min(n, e + H);
                cudaStream_t sc = st[c % st.size()];
                CUDA_CHECK(cudaMemcpyAsync(d_text + s, h_text + s, lim - s, cudaMemcpyHostToDevice, sc));
                const Launch l{flags, block, grid, smem, sc};
                launch<false>(l, t, d_text + s, lim - s, e - s, s, max_len, d_sum);
            }
            CUDA_CHECK(cudaDeviceSynchronize());
            CUDA_CHECK(cudaEventRecord(e2));
            CUDA_CHECK(cudaMemcpy(h_sum, d_sum, sizeof h_sum, cudaMemcpyDeviceToHost));
            CUDA_CHECK(cudaEventRecord(e3));
            CUDA_CHECK(cudaEventSynchronize(e3));
            r.h2d_ms = elapsed(e0, e1);      // automaton only
            r.search_ms = elapsed(e1, e2);   // overlapped text transfer + kernels
            r.d2h_ms = elapsed(e2, e3);
        }
        r.result = list ? summarize(last) : Summary{h_sum[0], h_sum[1]};
        if (list && r.result.count != h_sum[0]) throw std::runtime_error("list pass and count pass disagree");
        r.total_ms = r.io_ms + r.build_ms + r.h2d_ms + r.search_ms + r.d2h_ms;
        char extra[384];
        std::snprintf(extra, sizeof extra, "gpu=%s;sm=%d;grid=%d;streams=%d;smem=%zu", prop.name,
                      prop.multiProcessorCount, grid, streams, smem);
        r.extra = extra;

        if (cli.on("verify") && !(Summary{h_sum[0], h_sum[1]} == want)) {
            std::fprintf(stderr, "sigscan-cuda: VERIFY FAILED: gpu %llu/%016llx vs cpu %llu/%016llx\n", h_sum[0],
                         h_sum[1], (unsigned long long)want.count, (unsigned long long)want.checksum);
            return 2;
        }
        if (rep >= 0) emit(cli, r);
    }
    if (list && !cli.str("out").empty()) write_matches(cli.str("out"), last);
    for (auto& s : st) CUDA_CHECK(cudaStreamDestroy(s));
    for (void* p : {(void*)d_next, (void*)d_own_cnt, (void*)d_own_h, (void*)d_own_begin, (void*)d_own_ids})
        if (p) CUDA_CHECK(cudaFree(p));
    CUDA_CHECK(cudaFree(d_text));
    CUDA_CHECK(cudaFree(d_sum));
    CUDA_CHECK(cudaFreeHost(h_text));
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "sigscan-cuda: %s\n", e.what());
    return 1;
}
