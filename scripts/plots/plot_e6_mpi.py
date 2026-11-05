"""E6: local MPI speedup and time of each phase."""
import matplotlib.pyplot as plt
import pandas as pd

from style import (DASHED, FULL_W, GRAY, ORANGE, SOLID, comma_formatter, ideal_speedup, line, load, pe_boundary,
                   save, summary)

def main():
    df = load("e6_mpi")
    seq = df[df.label == "seq"]
    m = df[df.label == "mpi"].copy()
    extra = m.extra.str.split(";").map(lambda kv: {k: float(v) for k, v in (x.split("=") for x in kv)})
    for k in ("bcast_ms", "scatter_ms", "halo_ms", "gather_ms"):
        m[k] = extra.map(lambda d: d[k])
    m["rest_ms"] = m.bcast_ms + m.build_ms

    fig, (a, e, b) = plt.subplots(1, 3, figsize=(FULL_W, 2.9))
    ideal_speedup(a, 10)
    for col, ref, ls, fill, name in (("search_ms", seq.search_ms.median(), SOLID, True, "sam skan"),
                                     ("e2e_ms", seq.e2e_ms.median(), DASHED, False, "czas pełny")):
        s = summary(m, "np", col).sort_values("np")
        sp = pd.DataFrame({"med": ref / s.med, "q1": ref / s.q3, "q3": ref / s.q1})
        line(a, s.np, sp, ORANGE, "^", name, ls=ls, fill=fill)
        ef = sp.copy()
        for c in ("med", "q1", "q3"):
            ef[c] = sp[c] / s.np.values
        line(e, s.np, ef, ORANGE, "^", name, ls=ls, fill=fill)
    pe_boundary(a, where="bottom")
    pe_boundary(e, label=False)
    e.set_xticks(range(1, 11), [str(n) if n % 2 else "" for n in range(1, 11)])
    e.set_xlabel("Liczba procesów $np$")
    e.set_ylabel("Efektywność $E(np)$")
    e.set_title("(b) efektywność", loc="left")
    e.set_ylim(0, 1.15)
    e.yaxis.set_major_formatter(comma_formatter())
    a.set_xticks(range(1, 11), [str(n) if n % 2 else "" for n in range(1, 11)])
    a.set_xlabel("Liczba procesów $np$")
    a.set_ylabel("Przyspieszenie $S(np)$")
    a.set_title("(a) przyspieszenie", loc="left")
    a.legend(loc="upper left", fontsize=6.8)
    a.set_ylim(0, 10.5)
    a.yaxis.set_major_formatter(comma_formatter())

    nps = sorted(int(n) for n in m.np.unique())
    m["other_ms"] = m.halo_ms + m.gather_ms
    for col, name, c, mk, ls, fill in (("search_ms", "skan", GRAY, "s", SOLID, True),
                                       ("scatter_ms", "rozesłanie tekstu (Scatterv)", ORANGE, "^", SOLID, True),
                                       ("rest_ms", "wzorce (Bcast) + budowa automatu", GRAY, "o", DASHED, False),
                                       ("other_ms", "zakładki (Alltoallv) + wyniki (Reduce)", ORANGE, "D", DASHED,
                                        False)):
        s = summary(m, "np", col).sort_values("np")
        line(b, s.np, s, c, mk, name, ls=ls, fill=fill)
    b.set_yscale("log")
    b.set_xticks(nps, [str(n) if n % 2 else "" for n in nps])
    b.set_xlabel("Liczba procesów $np$")
    b.set_ylabel("Czas fazy [ms] (log)")
    b.set_title("(c) czas faz", loc="left")
    fig.tight_layout(w_pad=1.2)
    fig.subplots_adjust(bottom=0.3)
    handles, labels = b.get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=2, bbox_to_anchor=(0.5, 0.0), fontsize=7,
               title="(c) fazy:", title_fontsize=7)
    b.yaxis.set_major_formatter(comma_formatter(3))
    save(fig, "e6_mpi")


if __name__ == "__main__":
    main()
