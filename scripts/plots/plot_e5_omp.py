"""E5: OpenMP speedup and efficiency for p = 1..10, plus the interleaved scan."""
import matplotlib.pyplot as plt
import pandas as pd

from style import (BLUE, DASHED, FULL_W, HALF_W, SCHED, SOLID, comma_formatter, end_label, ideal_speedup, line,
                   load, pe_boundary, save, summary)

LABEL_SCHED = {"omp-blocks": "blocks", "omp-static-1M": "static", "omp-dynamic-1M": "dynamic",
               "omp-guided-1M": "guided"}


def speedup_frame(d, t_ref):
    s = summary(d, "threads", "search_ms").sort_values("threads")
    return pd.DataFrame({"threads": s.threads, "med": t_ref / s.med, "q1": t_ref / s.q3, "q3": t_ref / s.q1})


def main():
    df = load("e5_omp")
    t_seq = df[df.label == "seq"].search_ms.median()

    fig, (a, b) = plt.subplots(1, 2, figsize=(FULL_W, 2.6))
    ideal_speedup(a, 10)
    for label, key in LABEL_SCHED.items():
        c, m, name = SCHED[key]
        s = speedup_frame(df[df.label == label], t_seq)
        line(a, s.threads, s, c, m, name)
        e = s.copy()
        for col in ("med", "q1", "q3"):
            e[col] = s[col] / s.threads
        line(b, e.threads, e, c, m, name)
    for ax in (a, b):
        pe_boundary(ax)
        ax.set_xticks(range(1, 11))
        ax.set_xlabel("Liczba wątków $p$")
        ax.yaxis.set_major_formatter(comma_formatter())
    a.set_ylabel("Przyspieszenie $S(p) = T_1 / T_p$")
    a.set_title("(a) przyspieszenie", loc="left")
    b.set_ylabel("Efektywność $E(p) = S(p)/p$")
    b.set_title("(b) efektywność", loc="left")
    b.set_ylim(0, 1.15)
    a.legend(loc="upper left")
    save(fig, "e5_omp_speedup")

    fig, ax = plt.subplots(figsize=(HALF_W, 2.5))
    for label, ls, fill, name in (("omp-dynamic-1M", SOLID, True, "zwykły skan"),
                                  ("omp-dynamic-1M-il8", DASHED, False, "przeplot $K=8$")):
        s = summary(df[df.label == label], "threads", "search_gbs").sort_values("threads")
        line(ax, s.threads, s, BLUE, "s", name, ls=ls, fill=fill)
        row = s[s.threads == 4].iloc[0]
        end_label(ax, 4, row.med, name, BLUE, dx=-6, dy=7, ha="right")
    pe_boundary(ax, where="bottom")
    ax.set_xticks(range(1, 11))
    ax.set_xlabel("Liczba wątków $p$ (dynamic, kawałek 1 MiB)")
    ax.set_ylabel("Przepustowość skanu [GB/s]")
    ax.yaxis.set_major_formatter(comma_formatter())
    save(fig, "e5_omp_interleave")


if __name__ == "__main__":
    main()
