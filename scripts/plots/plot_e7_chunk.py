"""E7: scan time vs chunk size for static and dynamic scheduling (the U-curve)."""
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import LogFormatterMathtext

from style import (DASHED, FULL_W, GRAY, INK, SCHED, SOLID, bytes_axis, comma_formatter, line, load, save,
                   summary)

AQUA_RAMP = {8: "#8ed8bb", 32: "#1baf7a", 64: "#0a6345"}


def datasets(df):
    hot = df["hot-region"].astype(str) != "-"
    return {
        "uniform": df[~hot & (df.minlen == 8) & (df.maxlen == 32)],
        "hot": df[hot],
        8: df[(df.maxlen == 8)],
        32: df[~hot & (df.maxlen == 32)],
        64: df[(df.maxlen == 64)],
    }


def u_panel(ax, d, title):
    for p, ls, fill in ((10, SOLID, True), (8, DASHED, False)):
        blk = d[(d.label == "blocks") & (d.threads == p)].search_ms
        ax.axhline(blk.median(), color=GRAY, ls=ls, lw=1.0, zorder=1, label=f"bloki (1 na wątek), $p={p}$")
        for sched in ("static", "dynamic"):
            c, m, name = SCHED[sched]
            s = summary(d[(d.label == sched) & (d.threads == p)], "chunk", "search_ms").sort_values("chunk")
            line(ax, s.chunk, s, c, m, f"{name}, $p={p}$", ls=ls, fill=fill)
            if p == 10:
                best = s.loc[s.med.idxmin()]
                ax.plot(best.chunk, best.med, "o", ms=10, mfc="none", mec=INK, mew=0.9, zorder=4)
    bytes_axis(ax, d[d.label != "blocks"].chunk.unique())
    ax.tick_params(axis="x", labelrotation=45)
    ax.set_ylabel("Czas skanu [ms]")
    ax.set_title(title, loc="left")
    ax.yaxis.set_major_formatter(comma_formatter())


def main():
    df = load("e7_chunk")
    ds = datasets(df)
    fig, axes = plt.subplots(2, 2, figsize=(FULL_W, 5.0))
    u_panel(axes[0, 0], ds["uniform"], "(a) dane jednorodne")
    u_panel(axes[0, 1], ds["hot"], "(b) gorący obszar 64 MiB (gęste trafienia)")
    handles, labels = axes[0, 0].get_legend_handles_labels()
    order = [1, 2, 4, 5, 0, 3]
    fig.legend([handles[i] for i in order], [labels[i] for i in order], loc="lower center", ncol=3,
               bbox_to_anchor=(0.5, 1.0), fontsize=7.5)

    c_ax, o_ax = axes[1]
    for lmax, colour in AQUA_RAMP.items():
        d = ds[lmax]
        s = summary(d[(d.label == "dynamic") & (d.threads == 10)], "chunk", "search_ms").sort_values("chunk")
        rel = s.copy()
        for col in ("med", "q1", "q3"):
            rel[col] = s[col] / s.med.min()
        line(c_ax, s.chunk, rel, colour, "D", f"$L_{{max}} = {lmax}$")
        o = d[(d.label == "dynamic") & (d.threads == 10)].groupby("chunk").overlap_bytes.median()
        frac = 100 * o / d.text_bytes.iloc[0]
        o_ax.plot(frac.index, frac.values, color=colour, marker="D", label=f"$L_{{max}} = {lmax}$")
        x = np.array(sorted(frac.index))
        o_ax.plot(x, 100 * (lmax - 1) / x, color=colour, ls=DASHED, lw=0.9)
    bytes_axis(c_ax, ds[32][ds[32].label == "dynamic"].chunk.unique())
    c_ax.tick_params(axis="x", labelrotation=45)
    c_ax.set_ylabel("Czas / czas minimalny")
    c_ax.set_title("(c) dynamic, $p=10$: kształt krzywej vs $L_{max}$", loc="left")
    c_ax.legend(loc="upper center")
    c_ax.yaxis.set_major_formatter(comma_formatter())

    bytes_axis(o_ax, ds[32][ds[32].label == "dynamic"].chunk.unique())
    o_ax.tick_params(axis="x", labelrotation=45)
    o_ax.set_yscale("log")
    o_ax.set_ylabel("Ponownie skanowane bajty [% tekstu]")
    o_ax.set_title("(d) narzut zakładek (przerywana: $(L_{max}-1)/C$)", loc="left")
    o_ax.yaxis.set_major_formatter(LogFormatterMathtext())
    for ax in axes[1]:
        ax.set_xlabel("Rozmiar kawałka $C$")
    fig.tight_layout(h_pad=1.2)
    save(fig, "e7_chunk")


if __name__ == "__main__":
    main()
