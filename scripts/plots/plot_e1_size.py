"""E1: throughput vs text size, scan only and end-to-end."""
import matplotlib.pyplot as plt
import pandas as pd

from style import FULL_W, RAW, bottom_legend, bytes_axis, draw, load, log_axis, save, summary

MAC = ["seq", "omp-p10", "omp-p10-il8", "mpi-np8"]


def main():
    mac = load("e1_size")
    gpu = load("g1_size") if (RAW / "g1_size.csv").exists() else pd.DataFrame()
    fig, axes = plt.subplots(1, 2, figsize=(FULL_W, 3.4), sharey=True)
    for ax, col, title in zip(axes, ["search_gbs", "e2e_gbs"],
                              ["(a) sam skan / kernel", "(b) czas pełny (bez odczytu pliku)"]):
        for label in MAC:
            d = mac[mac.label == label]
            s = summary(d, "text_bytes", col).sort_values("text_bytes")
            draw(ax, s.text_bytes, s, label)
        if len(gpu):
            for label, name in (("cuda", "CUDA (T4)"), ("cuda-pipeline", None)):
                d = gpu[gpu.label == label]
                s = summary(d, "text_bytes", col).sort_values("text_bytes")
                draw(ax, s.text_bytes, s, label if (col == "search_gbs" or label != "cuda") else "cuda-e2e",
                     name=name if col == "search_gbs" else None)
        bytes_axis(ax, mac.text_bytes.unique())
        ax.tick_params(axis="x", labelrotation=45)
        ax.set_yscale("log")
        ax.set_xlabel("Rozmiar tekstu")
        ax.set_title(title, loc="left")
    axes[0].set_ylabel("Przepustowość [GB/s] (skala log)")
    log_axis(axes[0])
    bottom_legend(fig, axes[1], ncol=3, space=0.36)
    save(fig, "e1_size")


if __name__ == "__main__":
    main()
