#include "sigscan/automaton.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "sigscan/scan.hpp"

namespace sigscan {

std::size_t Outputs::bytes() const {
    return depth.size() * 4 + own_begin.size() * 4 + own_ids.size() * 4 + own_h.size() * 8 + out_link.size() * 4 +
           total.size() * 4 + sum_h.size() * 8 + sum_hl.size() * 8;
}

Trie build_trie(const PatternSet& ps) {
    // 1. insert patterns (sibling lists, root as an array)
    std::vector<u32> first_child{kNone}, next_sibling{kNone};
    std::vector<u8> label{0};
    std::array<u32, 256> root_child;
    root_child.fill(kNone);
    std::vector<std::pair<u32, u32>> terminals;  // (node, pid)
    terminals.reserve(ps.size());

    for (u32 pid = 0; pid < ps.size(); ++pid) {
        u32 v = 0;
        for (u8 c : ps.pats[pid]) {
            u32 w = kNone;
            if (v == 0) {
                w = root_child[c];
            } else {
                for (u32 x = first_child[v]; x != kNone; x = next_sibling[x])
                    if (label[x] == c) {
                        w = x;
                        break;
                    }
            }
            if (w == kNone) {
                w = u32(label.size());
                if (w >= kMask) throw std::runtime_error("automaton too large (>= 2^31 states)");
                label.push_back(c);
                first_child.push_back(kNone);
                next_sibling.push_back(kNone);
                if (v == 0) {
                    root_child[c] = w;
                } else {
                    next_sibling[w] = first_child[v];
                    first_child[v] = w;
                }
            }
            v = w;
        }
        terminals.emplace_back(v, pid);
    }

    // 2. BFS numbering, edges sorted by byte
    const u32 S = u32(label.size());
    Trie t;
    t.num_states = S;
    t.max_len = ps.max_len;
    std::vector<u32> order;  // order[new] = old
    order.reserve(S);
    std::vector<u32> new_id(S, kNone);
    order.push_back(0);
    new_id[0] = 0;
    t.edge_begin.reserve(S + 1);
    t.edge_byte.reserve(S - 1);
    t.edge_child.reserve(S - 1);
    std::vector<std::pair<u8, u32>> kids;
    for (std::size_t qi = 0; qi < order.size(); ++qi) {
        const u32 v = order[qi];
        kids.clear();
        if (v == 0) {
            for (int c = 0; c < 256; ++c)
                if (root_child[c] != kNone) kids.emplace_back(u8(c), root_child[c]);
        } else {
            for (u32 x = first_child[v]; x != kNone; x = next_sibling[x]) kids.emplace_back(label[x], x);
            std::sort(kids.begin(), kids.end());
        }
        t.edge_begin.push_back(u32(t.edge_byte.size()));
        for (auto [c, w] : kids) {
            new_id[w] = u32(order.size());
            order.push_back(w);
            t.edge_byte.push_back(c);
            t.edge_child.push_back(new_id[w]);
        }
    }
    t.edge_begin.push_back(u32(t.edge_byte.size()));

    // 3. failure links (BFS order, so fail[v] is already known)
    Outputs& o = t.out;
    o.depth.assign(S, 0);
    t.fail.assign(S, 0);
    auto find_child = [&](u32 v, u8 c) -> u32 {
        const auto b = t.edge_byte.begin() + t.edge_begin[v], e = t.edge_byte.begin() + t.edge_begin[v + 1];
        const auto it = std::lower_bound(b, e, c);
        return (it != e && *it == c) ? t.edge_child[std::size_t(it - t.edge_byte.begin())] : kNone;
    };
    for (u32 v = 0; v < S; ++v) {
        for (u32 k = t.edge_begin[v]; k < t.edge_begin[v + 1]; ++k) {
            const u8 c = t.edge_byte[k];
            const u32 w = t.edge_child[k];
            o.depth[w] = o.depth[v] + 1;
            if (v == 0) continue;  // depth 1 -> root
            for (u32 f = t.fail[v];; f = t.fail[f]) {
                if (const u32 x = find_child(f, c); x != kNone) {
                    t.fail[w] = x;
                    break;
                }
                if (f == 0) break;
            }
        }
    }

    // 4. outputs: own ids, output links, chain sums
    for (auto& [node, pid] : terminals) node = new_id[node];
    std::sort(terminals.begin(), terminals.end());
    o.own_begin.assign(S + 1, 0);
    o.own_ids.reserve(terminals.size());
    o.own_h.assign(S, 0);
    for (auto [node, pid] : terminals) {
        ++o.own_begin[node + 1];
        o.own_ids.push_back(pid);
        o.own_h[node] += pattern_hash(pid);
    }
    for (u32 v = 0; v < S; ++v) o.own_begin[v + 1] += o.own_begin[v];

    o.out_link.assign(S, kNone);
    o.total.assign(S, 0);
    o.sum_h.assign(S, 0);
    o.sum_hl.assign(S, 0);
    for (u32 v = 1; v < S; ++v) {
        const u32 f = t.fail[v];
        o.out_link[v] = o.has_own(f) ? f : o.out_link[f];
        o.total[v] = o.own_count(v) + o.total[f];
        o.sum_h[v] = o.own_h[v] + o.sum_h[f];
        o.sum_hl[v] = o.own_h[v] * o.depth[v] + o.sum_hl[f];
    }
    return t;
}

DenseAC build_dense(const Trie& t) {
    DenseAC ac;
    ac.num_states = t.num_states;
    ac.max_len = t.max_len;
    ac.out = t.out;
    const auto flag = [&](u32 s) { return t.out.total[s] ? (s | kFlag) : s; };
    ac.next.assign(std::size_t(t.num_states) << 8, 0);
    for (u32 v = 0; v < t.num_states; ++v) {
        u32* row = ac.next.data() + (std::size_t(v) << 8);
        if (v != 0) std::copy_n(ac.next.data() + (std::size_t(t.fail[v]) << 8), 256, row);
        for (u32 k = t.edge_begin[v]; k < t.edge_begin[v + 1]; ++k) row[t.edge_byte[k]] = flag(t.edge_child[k]);
    }
    return ac;
}

CompressedAC build_compressed(const Trie& t) {
    CompressedAC ac;
    ac.num_states = t.num_states;
    ac.max_len = t.max_len;
    ac.out = t.out;
    const auto flag = [&](u32 s) { return t.out.total[s] ? (s | kFlag) : s; };
    ac.root_row.fill(0);
    ac.nodes.resize(t.num_states);
    ac.child.resize(t.edge_child.size());
    for (u32 v = 0; v < t.num_states; ++v) {
        CNode& nd = ac.nodes[v];
        nd = CNode{{0, 0, 0, 0}, t.edge_begin[v], t.fail[v]};
        for (u32 k = t.edge_begin[v]; k < t.edge_begin[v + 1]; ++k) {
            const u8 c = t.edge_byte[k];
            nd.bits[c >> 6] |= 1ULL << (c & 63);
            ac.child[k] = flag(t.edge_child[k]);
            if (v == 0) ac.root_row[c] = flag(t.edge_child[k]);
        }
    }
    return ac;
}

PfacTable build_pfac(const Trie& t) {
    PfacTable pf;
    pf.num_states = t.num_states;
    pf.max_len = t.max_len;
    pf.out = t.out;
    pf.next.assign(std::size_t(t.num_states) << 8, 0);
    for (u32 v = 0; v < t.num_states; ++v)
        for (u32 k = t.edge_begin[v]; k < t.edge_begin[v + 1]; ++k) {
            const u32 w = t.edge_child[k];
            pf.next[(std::size_t(v) << 8) | t.edge_byte[k]] = t.out.has_own(w) ? (w | kFlag) : w;
        }
    return pf;
}

WorkStats measure_work(const PfacTable& pf, const DenseAC& ac, const u8* text, u64 n) {
    WorkStats ws;
    ws.starts = n;
    for (u64 i = 0; i < n; ++i) {
        u32 st = 0;
        u64 steps = 0;
        for (u64 j = i; j < n; ++j) {
            const u32 nx = pf.next[(std::size_t(st) << 8) | text[j]];
            if (nx == 0) break;
            st = nx & kMask;
            ++steps;
        }
        ws.pfac_steps += steps;
        ws.pfac_nonzero += steps > 0;
        ws.pfac_max = std::max(ws.pfac_max, steps);
    }
    u32 st = 0;
    for (u64 i = 0; i < n; ++i) {
        st = ac.step(st & kMask, text[i]);
        ws.ac_depth_sum += ac.out.depth[st & kMask];
    }
    return ws;
}

}  // namespace sigscan
