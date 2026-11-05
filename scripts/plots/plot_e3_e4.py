"""E3: throughput vs pattern length;  E4: throughput vs hit rate, count and list mode."""
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from style import FULL_W, HALF_W, RAW, bottom_legend, draw, load, log_axis, num, save

LENS = [(4, 8), (8, 16), (16, 32), (32, 64), (4, 64)]
HITS = [0, 1, 10, 100, 1000]


def gpu_or_empty(name):
    return load(name) if (RAW / f"{name}.csv").exists() else pd.DataFrame()


def by_len(d, col):
    rows = []
    for i, (a, b) in enumerate(LENS):
        v = d[(d.minlen == a) & (d.maxlen == b)][col]
        if len(v):
            rows.append({"x": i, "med": v.median(), "q1": v.quantile(0.25), "q3": v.quantile(0.75)})
    return pd.DataFrame(rows)


def by_hits(d, col):
    rows = []
    for i, h in enumerate(HITS):
        v = d[d["hit-rate"] == h][col]
        if len(v):
            rows.append({"x": i, "med": v.median(), "q1": v.quantile(0.25), "q3": v.quantile(0.75)})
    return pd.DataFrame(rows)


def main():
    mac, gpu = load("e3_len"), gpu_or_empty("g3_len")
    fig, ax = plt.subplots(figsize=(HALF_W, 2.6))
    for label in ("seq", "omp-p10", "omp-p10-il8", "mpi-np8"):
        s = by_len(mac[mac.label == label], "search_gbs")
        draw(ax, s.x, s, label)
    if len(gpu):
        s = by_len(gpu[gpu.label == "cuda"], "search_gbs")
        draw(ax, s.x, s, "cuda", name="CUDA (T4, kernel)")
    ax.set_xticks(range(len(LENS)), [f"{a}-{b}" for a, b in LENS])
    ax.set_xlabel("Długość wzorców [B]")
    ax.set_yscale("log")
    log_axis(ax)
    ax.set_ylabel("Przepustowość skanu [GB/s] (log)")
    ax.legend(loc="upper center", bbox_to_anchor=(0.5, -0.28), ncol=2, fontsize=6.8)
    save(fig, "e3_len")

    mac, gpu = load("e4_hits"), gpu_or_empty("g4_hits")
    fig, axes = plt.subplots(1, 2, figsize=(FULL_W, 3.0), sharey=True)
    for ax, mode, title in zip(axes, ("count", "list"), ("(a) tryb count (liczba + suma kontrolna)",
                                                         "(b) tryb list (pełna lista trafień)")):
        for label, key in (("seq", f"seq-{mode}"), ("omp-p10", f"omp-p10-{mode}"), ("mpi-np8", f"mpi-np8-{mode}")):
            s = by_hits(mac[mac.label == key], "e2e_gbs")
            draw(ax, s.x, s, label)
        if len(gpu):
            s = by_hits(gpu[gpu.label == f"cuda-{mode}"], "e2e_gbs")
            draw(ax, s.x, s, "cuda-e2e", name="CUDA (T4, z transferami)")
        ax.set_xticks(range(len(HITS)), ["0\n(brak)"] + [num(h) for h in HITS[1:]])
        ax.set_xlabel("Trafienia na MiB tekstu")
        ax.set_title(title, loc="left")
        ax.set_yscale("log")
    log_axis(axes[0])
    axes[0].set_ylabel("Przepustowość [GB/s] (czas pełny, log)")
    bottom_legend(fig, axes[1], ncol=4, space=0.27)
    save(fig, "e4_hits")


if __name__ == "__main__":
    np.seterr(all="ignore")
    main()
