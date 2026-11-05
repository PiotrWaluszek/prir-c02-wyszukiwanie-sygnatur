"""E10 (research question): when is the GPU (with transfers) faster than the best CPU version?

Grid of pattern length × partial matches, GPU vs OpenMP p=10 with 8 lanes: (a) kernel, (b) end-to-end.
"""
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.colors import LinearSegmentedColormap, TwoSlopeNorm

from style import BLUE, FULL_W, INK, load, num, save

LENS = [(4, 8), (8, 16), (16, 32), (32, 64)]
PARTIAL = [0, 300, 3000, 30000]
RED = "#e34948"
DIVERGING = LinearSegmentedColormap.from_list("div", [BLUE, "#9cc1ee", "#f0efec", "#f2a3a2", RED])


def cell(df, a, b, pr, label, col):
    d = df[(df.minlen == a) & (df.maxlen == b) & (df["partial-rate"] == pr) & (df.label == label)]
    return d[col].median() if len(d) else np.nan


def heat(ax, grid, cmap, norm, fmt):
    im = ax.imshow(grid, cmap=cmap, norm=norm, aspect="auto", origin="lower")
    for i in range(grid.shape[0]):
        for j in range(grid.shape[1]):
            v = grid[i, j]
            if np.isfinite(v):
                ax.text(j, i, fmt(v), ha="center", va="center", fontsize=7.5, color=INK)
    ax.set_xticks(range(len(PARTIAL)), [num(p) if p else "0" for p in PARTIAL])
    ax.set_yticks(range(len(LENS)), [f"{a}-{b}" for a, b in LENS])
    ax.set_xlabel("Częściowe dopasowania na MiB")
    ax.grid(False)
    for s in ax.spines.values():
        s.set_visible(False)
    return im


def main():
    cpu, gpu = load("e10_grid_cpu"), load("g10_grid")

    def ratio(gcol, ccol):
        return np.array([[cell(gpu, a, b, pr, "cuda", gcol) / cell(cpu, a, b, pr, "omp-p10-il8", ccol)
                          for pr in PARTIAL] for a, b in LENS])

    grids = [(np.log2(ratio("search_gbs", "search_gbs")), "(a) sam kernel / sam skan"),
             (np.log2(ratio("e2e_gbs", "e2e_gbs")), "(b) czas pełny (budowa, transfery, skan)")]
    m = max(max(abs(np.nanmin(g)), abs(np.nanmax(g))) for g, _ in grids)
    norm = TwoSlopeNorm(0, -m, m)
    fig, axes = plt.subplots(1, 2, figsize=(FULL_W, 2.6), sharey=True, constrained_layout=True)
    for ax, (g, title) in zip(axes, grids):
        im = heat(ax, g, DIVERGING, norm, lambda v: num(2 ** v, 2) + "×")
        ax.set_title(title, loc="left")
    axes[0].set_ylabel("Długość wzorców [B]")
    cb = fig.colorbar(im, ax=axes, fraction=0.04, pad=0.02)
    ticks = [t for t in (-2, -1, 0, 1, 2) if -m <= t <= m]
    cb.set_ticks(ticks, labels=[num(2.0 ** t, 2) + "×" for t in ticks])
    cb.set_label("GPU / CPU  (< 1: CPU szybszy)", fontsize=7.5)
    cb.outline.set_visible(False)
    save(fig, "e10_grid")


if __name__ == "__main__":
    main()
