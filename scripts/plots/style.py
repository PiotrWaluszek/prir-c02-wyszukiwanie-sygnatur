"""Plot style and helpers shared by all figures."""
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402
from matplotlib import font_manager  # noqa: E402
from matplotlib.ticker import FuncFormatter  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
RAW = ROOT / "results" / "raw"
FIG_DIR = ROOT / "results" / "figures"
TAB_DIR = ROOT / "results" / "tables"
PREVIEW_DIR = ROOT / "build" / "plots-preview"

FULL_W, HALF_W = 6.3, 3.1
INK, INK2, GRID = "#1f1f1e", "#52514e", "#e4e3df"

# colours, one per implementation
GRAY, BLUE, ORANGE, AQUA, YELLOW = "#52514e", "#2a78d6", "#eb6834", "#1baf7a", "#eda100"
IMPL = {
    # key: (colour, marker, display name)
    "seq": (GRAY, "o", "sekwencyjna"),
    "omp": (BLUE, "s", "OpenMP"),
    "mpi": (ORANGE, "^", "MPI"),
    "cuda": (AQUA, "D", "CUDA"),
    "cpu-colab": (YELLOW, "v", "CPU Colab"),
}
SCHED = {
    "blocks": (GRAY, "o", "bloki (1 na wątek)"),
    "static": (ORANGE, "^", "static"),
    "dynamic": (AQUA, "D", "dynamic"),
    "guided": (BLUE, "s", "guided"),
}
SOLID, DASHED, DOTTED = "-", (0, (4, 2)), (0, (1, 1.5))


def _setup():
    lm = Path("/usr/local/texlive")
    for f in sorted(lm.glob("*/texmf-dist/fonts/opentype/public/lm/lmroman10-*.otf")):
        font_manager.fontManager.addfont(str(f))
    plt.rcParams.update({
        "font.family": "serif",
        "font.serif": ["Latin Modern Roman", "CMU Serif", "DejaVu Serif"],
        "mathtext.fontset": "cm",
        "font.size": 8.5, "axes.titlesize": 9, "axes.labelsize": 8.5, "legend.fontsize": 7.5,
        "xtick.labelsize": 7.5, "ytick.labelsize": 7.5,
        "axes.edgecolor": INK2, "axes.labelcolor": INK, "xtick.color": INK2, "ytick.color": INK2,
        "text.color": INK, "axes.linewidth": 0.6,
        "axes.spines.top": False, "axes.spines.right": False,
        "axes.grid": True, "axes.grid.axis": "y", "grid.color": GRID, "grid.linewidth": 0.6,
        "axes.axisbelow": True,
        "lines.linewidth": 1.6, "lines.markersize": 4.5,
        "legend.frameon": False, "legend.handlelength": 2.4,
        "figure.dpi": 150, "savefig.bbox": "tight", "savefig.pad_inches": 0.02,
        "pdf.fonttype": 42, "axes.unicode_minus": True,
    })


_setup()


# --- number formatting (decimal comma) ---
def num(x, digits=1):
    if x == int(x) and abs(x) >= 1:
        s = f"{int(x):,}".replace(",", " ")
    else:
        s = f"{x:.{digits}f}"
    return s.replace(".", ",")


def comma_formatter(digits=2):
    def f(x, _):
        s = f"{x:.{digits}f}".rstrip("0").rstrip(".")
        if s in ("-0", ""):
            s = "0"
        if abs(x) >= 1000:
            s = f"{x:,.0f}".replace(",", "\u2009")
        return s.replace(".", ",")
    return FuncFormatter(f)


def fmt_bytes(b):
    for unit, k in (("GiB", 1 << 30), ("MiB", 1 << 20), ("KiB", 1 << 10)):
        if b >= k:
            v = b / k
            return f"{num(v, 0 if v == int(v) else 1)} {unit}"
    return f"{int(b)} B"


def bytes_axis(ax, values, axis="x"):
    values = sorted(set(int(v) for v in values))
    if axis == "x":
        ax.set_xscale("log", base=2)
        ax.set_xticks(values, [fmt_bytes(v) for v in values])
        ax.minorticks_off()
    else:
        ax.set_yscale("log", base=2)
        ax.set_yticks(values, [fmt_bytes(v) for v in values])


def log_axis(ax, axis="y"):
    lo, hi = ax.get_ylim() if axis == "y" else ax.get_xlim()
    ticks = [m * 10.0 ** e for e in range(-4, 5) for m in (1, 2, 5) if lo <= m * 10.0 ** e <= hi]
    if axis == "y":
        ax.set_yscale("log")
        ax.set_yticks(ticks)
        ax.yaxis.set_major_formatter(comma_formatter(4))
        ax.yaxis.set_minor_formatter(FuncFormatter(lambda *_: ""))
    else:
        ax.set_xscale("log")
        ax.set_xticks(ticks)
        ax.xaxis.set_major_formatter(comma_formatter(4))


def parse_size(s):
    s = str(s)
    units = {"k": 1 << 10, "M": 1 << 20, "G": 1 << 30}
    return int(float(s[:-1]) * units[s[-1]]) if s[-1] in units else int(float(s))


# --- data ---
def load(exp, stats=False):
    path = RAW / f"{exp}{'_stats' if stats else ''}.csv"
    df = pd.read_csv(path)
    if "tag" in df:
        tags = df["tag"].fillna("").map(lambda t: dict(kv.split("=", 1) for kv in t.split(";") if kv))
        tag_df = pd.DataFrame(list(tags), index=df.index)
        for c in tag_df.columns:
            if c not in df.columns:
                try:
                    df[c] = pd.to_numeric(tag_df[c])
                except (ValueError, TypeError):
                    df[c] = tag_df[c]
    if "search_ms" in df:
        df["search_gbs"] = df["text_bytes"] / df["search_ms"] / 1e6
        df["total_gbs"] = df["text_bytes"] / df["total_ms"] / 1e6
        df["e2e_ms"] = df["total_ms"] - df["io_ms"]
        df["e2e_gbs"] = df["text_bytes"] / df["e2e_ms"] / 1e6
    return df


def summary(df, by, col):
    g = df.groupby(by, dropna=False)[col]
    out = g.median().rename("med").to_frame()
    out["q1"] = g.quantile(0.25)
    out["q3"] = g.quantile(0.75)
    out["n"] = g.size()
    return out.reset_index()


# --- drawing ---
LABELS = {
    "seq": (GRAY, "o", SOLID, True, "sekwencyjna"),
    "seq-dense": (GRAY, "o", SOLID, True, "sekwencyjna, gęsty"),
    "seq-compressed": (GRAY, "o", DASHED, False, "sekwencyjna, skompresowany"),
    "omp-p8": (BLUE, "s", SOLID, False, "OpenMP $p=8$, bloki"),
    "omp-p10": (BLUE, "s", SOLID, True, "OpenMP $p=10$"),
    "omp-p10-compressed": (BLUE, "s", DASHED, False, "OpenMP $p=10$, skompresowany"),
    "omp-p10-il8": (BLUE, "s", DASHED, False, "OpenMP $p=10$ + przeplot 8"),
    "mpi": (ORANGE, "^", SOLID, True, "MPI"),
    "mpi-np8": (ORANGE, "^", SOLID, True, "MPI $np=8$"),
    "cuda": (AQUA, "D", SOLID, True, "CUDA (kernel)"),
    "cuda-e2e": (AQUA, "D", DASHED, False, "CUDA (z transferami)"),
    "cuda-pipeline": (AQUA, "D", DOTTED, False, "CUDA, potok 4 strumienie"),
    "seq-colab": (YELLOW, "v", SOLID, True, "sekwencyjna (Colab)"),
    "seq-colab-il8": (YELLOW, "v", DASHED, False, "sekwencyjna + przeplot (Colab)"),
    "omp-colab-p2": (YELLOW, "v", SOLID, True, "OpenMP $p=2$ (Colab)"),
    "omp-colab-p2-il8": (YELLOW, "v", DASHED, False, "OpenMP $p=2$ + przeplot (Colab)"),
}


def draw(ax, x, s, label, name=None, err=True):
    c, m, ls, fill, default = LABELS[label]
    line(ax, x, s, c, m, name or default, ls=ls, fill=fill, err=err)


def line(ax, x, s, colour, marker, label=None, ls=SOLID, fill=True, err=True, zorder=3):
    kw = dict(color=colour, marker=marker, ls=ls, label=label, zorder=zorder,
              markerfacecolor=colour if fill else "white", markeredgecolor=colour, markeredgewidth=1.1)
    ax.plot(x, s["med"], **kw)
    if err:
        ax.errorbar(x, s["med"], yerr=[s["med"] - s["q1"], s["q3"] - s["med"]], fmt="none",
                    ecolor=colour, elinewidth=0.9, capsize=1.8, zorder=zorder)


def end_label(ax, x, y, text, colour, dx=4, dy=0, ha="left"):
    ax.annotate(text, (x, y), xytext=(dx, dy), textcoords="offset points", va="center", ha=ha,
                fontsize=7.5, color=INK)


def ideal_speedup(ax, pmax):
    p = np.arange(1, pmax + 1)
    ax.plot(p, p, ls=DASHED, color="#a3a29c", lw=1.0, zorder=1, label="idealne $S=p$")


def pe_boundary(ax, p=8, label=True, where="top"):
    ax.axvline(p, color="#a3a29c", lw=0.8, ls=DOTTED, zorder=1)
    if label:
        top = where == "top"
        ax.annotate("8 P | 2 E", (p, 1 if top else 0), xycoords=("data", "axes fraction"),
                    xytext=(3, -4 if top else 4), textcoords="offset points", ha="left",
                    va="top" if top else "bottom", fontsize=7, color=INK2)


def bottom_legend(fig, ax, ncol=3, space=0.3):
    fig.tight_layout()
    fig.subplots_adjust(bottom=space)
    handles, labels = ax.get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=ncol, bbox_to_anchor=(0.5, 0.0), fontsize=7.5)


def save(fig, name):
    FIG_DIR.mkdir(parents=True, exist_ok=True)
    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
    fig.savefig(FIG_DIR / f"{name}.pdf")
    fig.savefig(PREVIEW_DIR / f"{name}.png", dpi=170)
    plt.close(fig)
    print(f"  {name}.pdf")


def write_table(name, text):
    TAB_DIR.mkdir(parents=True, exist_ok=True)
    (TAB_DIR / f"{name}.tex").write_text(text.replace("\u2009", "\\,"))
    print(f"  {name}.tex")
