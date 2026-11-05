#!/usr/bin/env python3
"""Checks all measurements: on the same data set every run on every machine must report the same
number of matches and the same checksum."""
import csv
import sys
from collections import defaultdict
from pathlib import Path

RAW = Path(__file__).resolve().parents[1] / "results" / "raw"


def main():
    seen = defaultdict(lambda: defaultdict(set))
    rows = 0
    for path in sorted(RAW.glob("*.csv")):
        if path.name.endswith("_stats.csv"):
            continue
        with open(path) as f:
            reader = csv.DictReader(f)
            if "matches" not in (reader.fieldnames or []):
                continue
            for r in reader:
                seen[r["dataset"]][(r["matches"], r["checksum"])].add((path.stem, r.get("label", r["impl"]), r["host"]))
                rows += 1
    bad = {d: v for d, v in seen.items() if len(v) > 1}
    hosts_of = {d: {h for runs in v.values() for _, _, h in runs} for d, v in seen.items()}
    hosts = set().union(*hosts_of.values()) if hosts_of else set()
    shared = sum(1 for h in hosts_of.values() if len(h) > 1)
    print(f"{rows} measurements, {len(seen)} data sets, {len(hosts)} hosts, {shared} data sets measured on >1 host")
    for d, v in bad.items():
        print(f"MISMATCH {d}:")
        for res, runs in v.items():
            print(f"    {res}: {sorted(runs)[:6]}")
    print("ALL CONSISTENT" if not bad else f"{len(bad)} INCONSISTENT DATA SET(S)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
