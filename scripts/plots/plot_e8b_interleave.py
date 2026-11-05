"""E8b: K interleaved automaton walks in one thread."""
import matplotlib.pyplot as plt

from style import AQUA, BLUE, FULL_W, ORANGE, SOLID, comma_formatter, end_label, fmt_bytes, line, load, num, save, summary

NPAT_STYLE = {100: (BLUE, "o"), 1000: (ORANGE, "s"), 10000: (AQUA, "D")}


def main():
    df = load("e8b_interleave")
    fig, axes = plt.subplots(1, 2, figsize=(FULL_W, 2.5), sharey=True)
    for ax, layout, title in zip(axes, ["dense", "compressed"], ["(a) automat gęsty (DFA)", "(b) automat skompresowany"]):
        d = df[df.layout == layout]
        for npat, (c, m) in NPAT_STYLE.items():
            s = summary(d[d.npat == npat], "interleave", "search_gbs").sort_values("interleave")
            sizes = [fmt_bytes(df[(df.npat == npat) & (df.layout == l)].automaton_bytes.iloc[0])
                     for l in ("dense", "compressed")]
            line(ax, s.interleave, s, c, m, f"{num(npat)} wzorców (DFA {sizes[0]}, skompr. {sizes[1]})", ls=SOLID)
            if layout == "dense":
                end_label(ax, s.interleave.iloc[-1], s.med.iloc[-1], num(npat), c,
                          dy={1000: 4, 10000: -4}.get(npat, 0))
        ax.set_xscale("log", base=2)
        ax.set_xticks([1, 2, 4, 8, 16], ["1", "2", "4", "8", "16"])
        ax.minorticks_off()
        ax.set_xlabel("Liczba przeplatanych ścieżek $K$ w jednym wątku")
        ax.set_title(title, loc="left")
        ax.yaxis.set_major_formatter(comma_formatter())
    axes[0].set_ylabel("Przepustowość skanu [GB/s]")
    axes[1].legend(loc="upper left")
    save(fig, "e8b_interleave")


if __name__ == "__main__":
    main()
