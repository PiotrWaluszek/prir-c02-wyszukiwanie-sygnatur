"""E9 (GPU, Colab T4): kernel variants × block size, divergence, transfer pipeline."""
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LinearSegmentedColormap

from style import (AQUA, BLUE, FULL_W, HALF_W, INK, INK2, YELLOW, comma_formatter, line, load, num, parse_size,
                   save, summary)

KERNELS = ["global", "ldg", "smem-text", "smem-root", "smem-all"]
KERNEL_NAMES = {"global": "global", "ldg": "ldg (cache tylko do odczytu)", "smem-text": "tekst w smem",
                "smem-root": "korzeń w smem", "smem-all": "ldg + tekst + korzeń"}
BLUES = LinearSegmentedColormap.from_list("blues", ["#eef4fc", "#9cc1ee", BLUE, "#123f78"])


def g5_block():
    df = load("g5_block")
    blocks = sorted(df.block.unique())
    grid = np.array([[df[(df.variant == k) & (df.block == b)].search_gbs.median() for b in blocks] for k in KERNELS])
    fig, ax = plt.subplots(figsize=(HALF_W + 0.4, 2.3))
    im = ax.imshow(grid, cmap=BLUES, aspect="auto")
    for i in range(len(KERNELS)):
        for j in range(len(blocks)):
            v = grid[i, j]
            dark = (v - grid.min()) / (grid.max() - grid.min() + 1e-9) > 0.55
            weight = "bold" if v == grid.max() else "normal"
            ax.text(j, i, num(v, 1), ha="center", va="center", fontsize=7, color="white" if dark else INK,
                    fontweight=weight)
    ax.set_xticks(range(len(blocks)), [str(b) for b in blocks])
    ax.set_yticks(range(len(KERNELS)), [KERNEL_NAMES[k] for k in KERNELS])
    ax.set_xlabel("Wątki w bloku")
    ax.grid(False)
    for s in ax.spines.values():
        s.set_visible(False)
    cb = fig.colorbar(im, ax=ax, fraction=0.05, pad=0.03)
    cb.set_label("Przepustowość kernela [GB/s]")
    cb.formatter = comma_formatter()
    cb.update_ticks()
    cb.outline.set_visible(False)
    save(fig, "g5_block")


def g6_divergence():
    df = load("g6_divergence")
    st = load("g6_divergence", stats=True)
    fig, ax = plt.subplots(figsize=(HALF_W, 2.6))
    pts = []
    for tag, g in df.groupby("tag"):
        s = st[st.tag == tag].iloc[0]
        pts.append((s.pfac_mean_steps, g[g.label == "cuda"].search_gbs.median(),
                    g[g.label == "seq-colab"].search_gbs.median(), tag))
    pts.sort()
    x = [p[0] for p in pts]
    ax.plot(x, [p[1] for p in pts], color=AQUA, marker="D", label="CUDA (kernel)")
    ax.plot(x, [p[2] for p in pts], color=YELLOW, marker="v", label="sekwencyjna (Colab)")
    for px, py, _, tag in pts:
        kv = dict(t.split("=") for t in tag.split(";"))
        label = f"alfabet {kv['alphabet']}" if kv["alphabet"] != "full" else f"prefiksy {kv['prefix-share']}"
        label = label.replace("alfabet dna", "DNA").replace("alfabet ascii", "ASCII").replace("alfabet 16", "16 symboli")
        off = {"prefiksy 0.0": (4, -11), "prefiksy 0.3": (-6, -14), "ASCII": (5, 4), "prefiksy 0.6": (4, 13),
               "prefiksy 0.9": (4, 4)}.get(label, (4, 4))
        ax.annotate(label.replace(".", ","), (px, py), xytext=off, textcoords="offset points", fontsize=6.5,
                    color=INK2, ha="right" if off[0] < 0 else "left")
    ax.set_yscale("log")
    ax.set_ylim(None, 120)
    ax.yaxis.set_major_formatter(comma_formatter())
    ax.xaxis.set_major_formatter(comma_formatter())
    ax.set_xlabel("Średnia długość ścieżki PFAC na pozycję")
    ax.set_ylabel("Przepustowość skanu [GB/s] (log)")
    ax.legend(loc="center right", fontsize=7)
    save(fig, "g6_divergence")


def g7_pipeline():
    df = load("g7_pipeline")
    df["gpu_ms"] = df.h2d_ms + df.search_ms + df.d2h_ms
    one = df[df.label == "cuda"]
    pipe = df[df.label == "cuda-pipeline"]
    fig, (a, b) = plt.subplots(1, 2, figsize=(FULL_W, 2.5), gridspec_kw={"width_ratios": [0.6, 1.4]})
    parts = [("h2d_ms", "H2D (tekst + automat)", "#a3a29c"), ("search_ms", "kernel", AQUA), ("d2h_ms", "D2H", INK2)]
    bottom = 0
    for col, name, c in parts:
        v = one[col].median()
        a.bar([0], [v], bottom=bottom, color=c, width=0.5, label=name, edgecolor="white", linewidth=0.8)
        bottom += v
    a.set_xticks([0], ["1 transfer\n+ 1 kernel"])
    a.set_xlim(-0.6, 0.6)
    a.set_ylabel("Czas [ms] (mediana)")
    a.set_title("(a) bez potoku", loc="left")
    a.legend(loc="upper center", bbox_to_anchor=(0.5, -0.25), fontsize=7)
    a.yaxis.set_major_formatter(comma_formatter())

    ramp = {"4M": "#8ed8bb", "16M": AQUA, "64M": "#0a6345"}
    for chunk, c in ramp.items():
        s = summary(pipe[pipe.chunk == parse_size(chunk)], "streams", "gpu_ms").sort_values("streams")
        line(b, s.streams, s, c, "D", f"kawałek {chunk.replace('M', ' MiB')}")
    b.axhline(one.gpu_ms.median(), color=INK2, ls=(0, (4, 2)), lw=1)
    b.annotate("bez potoku (H2D + kernel + D2H)", (0.02, one.gpu_ms.median()), xycoords=("axes fraction", "data"),
               xytext=(0, 3), textcoords="offset points", ha="left", fontsize=7, color=INK)
    b.axhline(one.h2d_ms.median(), color=INK2, ls=(0, (1, 1.5)), lw=1)
    b.annotate("sam transfer H2D, czyli dolna granica", (0.02, one.h2d_ms.median()), xycoords=("axes fraction", "data"),
               xytext=(0, -3), textcoords="offset points", ha="left", va="top", fontsize=7, color=INK)
    b.set_xscale("log", base=2)
    b.set_xticks([1, 2, 4, 8], ["1", "2", "4", "8"])
    b.minorticks_off()
    b.set_xlabel("Liczba strumieni CUDA")
    b.set_ylabel("H2D + kernel + D2H [ms]")
    b.set_title("(b) potok: kopiowanie kawałków równolegle z kernelem", loc="left")
    b.legend(loc="lower right", fontsize=7)
    for ax in (a, b):
        ax.set_ylim(0, one.gpu_ms.median() * 1.12)
    b.yaxis.set_major_formatter(comma_formatter())
    save(fig, "g7_pipeline")


def main():
    for f in (g5_block, g6_divergence, g7_pipeline):
        try:
            f()
        except FileNotFoundError as e:
            print(f"  skip {f.__name__}: {e.filename} missing")


if __name__ == "__main__":
    main()
