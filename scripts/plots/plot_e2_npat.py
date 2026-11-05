"""E2 + E8: throughput and automaton size vs number of patterns; dense vs compressed layout."""
import matplotlib.pyplot as plt
import pandas as pd

from style import (AQUA, DASHED, DOTTED, FULL_W, GRAY, HALF_W, INK2, RAW, SOLID, comma_formatter, draw, fmt_bytes,
                   line, load, num, save, summary)

NPAT_TICKS = [10, 100, 1000, 10000, 100000]


def npat_axis(ax):
    ax.set_xscale("log")
    ax.set_xticks(NPAT_TICKS, [num(v) for v in NPAT_TICKS])
    ax.minorticks_off()
    ax.set_xlabel("Liczba wzorców")


def main():
    mac = load("e2_npat")
    stats = load("e2_npat", stats=True)
    gpu = load("g2_npat") if (RAW / "g2_npat.csv").exists() else pd.DataFrame()

    fig, (a, b) = plt.subplots(1, 2, figsize=(FULL_W, 2.7))
    for label in ("seq-dense", "omp-p10", "omp-p10-il8", "mpi-np8"):
        s = summary(mac[mac.label == label], "npat", "search_gbs").sort_values("npat")
        draw(a, s.npat, s, label, name="sekwencyjna" if label == "seq-dense" else None)
    if len(gpu):
        s = summary(gpu[gpu.label == "cuda"], "npat", "search_gbs").sort_values("npat")
        draw(a, s.npat, s, "cuda", name="CUDA (T4, kernel)")
    a.set_yscale("log")
    a.set_ylim(4e-4, 150)
    a.yaxis.set_major_formatter(comma_formatter(4))
    a.set_ylabel("Przepustowość skanu [GB/s] (log)")
    a.set_title("(a) przepustowość", loc="left")
    a.legend(loc="lower left", fontsize=7)
    npat_axis(a)

    st = stats.sort_values("npat")
    b.plot(st.npat, st.dense_bytes, color=GRAY, marker="o", ls=SOLID, label="gęsty DFA (CPU)")
    b.plot(st.npat, st.compressed_bytes, color=GRAY, marker="o", mfc="white", ls=DASHED, label="skompresowany (CPU)")
    b.plot(st.npat, st.pfac_table_bytes, color=AQUA, marker="D", ls=SOLID, label="tablica PFAC (GPU)")
    for y, text in ((12 << 20, "L2 M1 Pro: 12 MiB"), (4 << 20, "L2 T4: 4 MiB")):
        b.axhline(y, color=INK2, lw=0.8, ls=DOTTED)
        b.annotate(text, (10, y), xytext=(0, 2), textcoords="offset points", fontsize=6.8, color=INK2,
                   va="bottom", ha="left")
    b.set_yscale("log", base=2)
    ticks = [1 << k for k in range(10, 34, 4)]
    b.set_yticks(ticks, [fmt_bytes(t) for t in ticks])
    b.set_ylabel("Rozmiar struktury")
    b.set_title("(b) rozmiar automatu", loc="left")
    b.legend(loc="lower right", fontsize=7)
    npat_axis(b)
    fig.tight_layout(w_pad=2.0)
    save(fig, "e2_npat")

    # E8: dense vs compressed throughput
    fig, ax = plt.subplots(figsize=(HALF_W, 2.5))
    for label in ("seq-dense", "seq-compressed", "omp-p10", "omp-p10-compressed"):
        s = summary(mac[mac.label == label], "npat", "search_gbs").sort_values("npat")
        draw(ax, s.npat, s, label, name={"omp-p10": "OpenMP $p=10$, gęsty"}.get(label))
    ax.set_yscale("log")
    ax.yaxis.set_major_formatter(comma_formatter())
    ax.set_ylabel("Przepustowość skanu [GB/s] (log)")
    ax.legend(loc="lower left", fontsize=6.8)
    npat_axis(ax)
    save(fig, "e8_layout")


if __name__ == "__main__":
    main()
