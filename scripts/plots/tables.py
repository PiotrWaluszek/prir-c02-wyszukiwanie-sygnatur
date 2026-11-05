"""LaTeX tables (booktabs) for the report: best results on the baseline data set."""
from style import RAW, load, num, write_table

BASE = 512 << 20

ROWS = [
    ("e5_omp", "seq", {}, "sekwencyjna", "M1 Pro, 1 rdzeń"),
    ("e5_omp", "omp-blocks", {"threads": 8}, "OpenMP, $p=8$, bloki", "M1 Pro, 8 P"),
    ("e5_omp", "omp-dynamic-1M", {"threads": 10}, "OpenMP, $p=10$, dynamic 1 MiB", "M1 Pro, 8 P + 2 E"),
    ("e5_omp", "omp-dynamic-1M-il8", {"threads": 10}, "OpenMP, $p=10$ + przeplot $K=8$", "M1 Pro, 8 P + 2 E"),
    ("e6_mpi", "mpi", {"procs": 8}, "MPI, $np=8$", "M1 Pro, 8 P"),
    ("g1_size", "seq-colab", {}, "sekwencyjna", "Colab, Xeon 2,0 GHz"),
    ("g1_size", "seq-colab-il8", {}, "sekwencyjna + przeplot $K=8$", "Colab, Xeon 2,0 GHz"),
    ("g1_size", "omp-colab-p2", {}, "OpenMP, $p=2$", "Colab, 2 vCPU"),
    ("g1_size", "omp-colab-p2-il8", {}, "OpenMP, $p=2$ + przeplot $K=8$", "Colab, 2 vCPU"),
    ("g1_size", "cuda", {}, "CUDA PFAC, 1 transfer", "Tesla T4"),
    ("g1_size", "cuda-pipeline", {}, "CUDA PFAC, potok 4 strumienie", "Tesla T4"),
]
REF_LABEL = {"e5_omp": "seq", "e6_mpi": "seq", "g1_size": "seq-colab"}


def best_results():
    data = {e: load(e) for e in {r[0] for r in ROWS} if (RAW / f"{e}.csv").exists()}
    base = {e: d[d.text_bytes == BASE] for e, d in data.items()}
    # speedup vs seq from the same machine and series
    refs = {e: d[d.label == REF_LABEL[e]] for e, d in base.items()}
    lines = []
    for exp, label, filt, name, hw in ROWS:
        if exp not in base:
            continue
        d = base[exp][base[exp].label == label]
        for k, v in filt.items():
            d = d[d[k] == v]
        if not len(d):
            continue
        ref = refs[exp]
        e2e = d.e2e_gbs.median()
        s_e2e = ref.e2e_ms.median() / d.e2e_ms.median()
        if label == "cuda-pipeline":
            scan, s_scan = "n/d", "n/d"
        else:
            scan = num(d.search_gbs.median(), 2)
            s_scan = num(ref.search_ms.median() / d.search_ms.median(), 1) + r"$\times$"
        lines.append(f"{name} & {hw} & {scan} & {num(e2e, 2)} & {s_scan} & {num(s_e2e, 1)}$\\times$ \\\\")
    body = "\n".join(lines)
    write_table("best_results", rf"""\begin{{tabular}}{{llrrrr}}
\toprule
 & & \multicolumn{{2}}{{c}}{{Przepustowość [GB/s]}} & \multicolumn{{2}}{{c}}{{Przyspieszenie}} \\
\cmidrule(lr){{3-4}}\cmidrule(lr){{5-6}}
Wersja & Sprzęt & skan & pełny & skan & pełny \\
\midrule
{body}
\bottomrule
\end{{tabular}}
""")


def build_times():
    mac, st = load("e2_npat"), load("e2_npat", stats=True)
    gpu = load("g2_npat") if (RAW / "g2_npat.csv").exists() else None
    lines = []
    for npat in sorted(mac.npat.unique()):
        d = mac[mac.npat == npat]
        states = int(st[st.npat == npat].states.iloc[0])
        dense = d[d.label == "seq-dense"]
        comp = d[d.label == "seq-compressed"]
        pfac = "n/d" if gpu is None else gpu[(gpu.npat == npat) & (gpu.label == "cuda")].build_ms.median()
        pfac = pfac if isinstance(pfac, str) else (num(pfac, 1) if pfac < 100 else num(round(pfac)))
        lines.append(f"{num(npat)} & {num(states)} & {num(dense.build_ms.median(), 2)} & "
                     f"{num(comp.build_ms.median(), 2)} & {pfac} & {num(round(dense.search_ms.median()))} & "
                     f"{num(round(comp.search_ms.median()))} \\\\")
    body = "\n".join(lines)
    write_table("build_times", rf"""\begin{{tabular}}{{rrrrrrr}}
\toprule
 & & \multicolumn{{3}}{{c}}{{Budowa automatu [ms]}} & \multicolumn{{2}}{{c}}{{Skan \SI{{256}}{{MiB}} [ms]}} \\
\cmidrule(lr){{3-5}}\cmidrule(lr){{6-7}}
Wzorce & Stany & DFA & skompr. & PFAC & DFA & skompr. \\
\midrule
{body}
\bottomrule
\end{{tabular}}
""")


def main():
    best_results()
    build_times()


if __name__ == "__main__":
    main()
