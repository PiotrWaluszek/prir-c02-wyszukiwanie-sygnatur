#!/usr/bin/env python3
"""Writes results/raw/datasets.csv: generator parameters (with the seed) of every data set used,
so each one can be regenerated with sigscan-gen."""
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "bench"))
import run  # noqa: E402

PARAMS = ["size", "npat", "minlen", "maxlen", "hit-rate", "partial-rate", "prefix-share", "alphabet",
          "no-match", "hot-region", "seed"]


def main():
    rows = {}
    for cfg_path in sorted((ROOT / "bench" / "configs").glob("*/*.json")):
        cfg = json.loads(cfg_path.read_text())
        for ds, _, _, _ in run.build_jobs(cfg):
            prefix, _ = run.dataset_files(ds, Path("data"))
            r = rows.setdefault(prefix.name, {"dataset": prefix.name, **{k: ds.get(k, "") for k in PARAMS},
                                              "experiments": set()})
            r["experiments"].add(cfg["name"])
    out = ROOT / "results" / "raw" / "datasets.csv"
    with open(out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["dataset", *PARAMS, "experiments"], lineterminator="\n")
        w.writeheader()
        for r in sorted(rows.values(), key=lambda r: r["dataset"]):
            w.writerow({**r, "experiments": " ".join(sorted(r["experiments"]))})
    used = set()
    for p in (ROOT / "results" / "raw").glob("*.csv"):
        if p.name != "datasets.csv":
            with open(p) as f:
                used |= {r["dataset"] for r in csv.DictReader(f)}
    missing = used - set(rows)
    print(f"{len(rows)} data sets -> {out.relative_to(ROOT)}; used in results: {len(used)}, undescribed: {len(missing)}")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
