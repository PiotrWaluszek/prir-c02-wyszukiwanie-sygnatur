#!/usr/bin/env python3
"""Benchmark driver: bench/configs/<exp>.json -> results/raw/<exp>.csv.

A config describes one experiment:
  {
    "name": "e5_omp_speedup",
    "dataset": {"size": "512M"},                  # overrides of BASE_DATASET
    "matrix":  {"size": ["16M", "64M"]},           # swept parameters (or a list of points)
    "reps": 7, "warmup": 1, "mode": "count",
    "runs": [
      {"impl": "seq"},
      {"impl": "omp", "label": "omp-static", "args": {"threads": [1, 2, 4, 8, 10], "schedule": "blocks"}},
      {"impl": "mpi", "np": [2, 4, 8], "max_size": "1G"},
      {"impl": "seq", "stats": true}               # --stats line -> results/raw/<exp>_stats.csv
    ]
  }
Lists in "args" / "np" give a cartesian product; "only" and "max_size" limit a run to some datasets.
Each CSV row gets three extra columns: exp, label and tag (swept parameters).

Datasets are cached in data/ under a hash of their parameters. All runs of a series are shuffled with
a fixed seed, so a slow drift of the machine does not follow any parameter.
"""
import argparse
import csv
import hashlib
import itertools
import json
import os
import random
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

BASE_DATASET = {
    "size": "512M", "npat": 1000, "minlen": 8, "maxlen": 32, "hit-rate": 10, "partial-rate": 0,
    "prefix-share": 0.3, "alphabet": "full", "seed": 2026,
}
UNITS = {"k": 1 << 10, "M": 1 << 20, "G": 1 << 30}


def parse_size(s):
    s = str(s)
    return int(float(s[:-1]) * UNITS[s[-1]]) if s[-1] in UNITS else int(s)


def product(d):
    keys = list(d)
    vals = [v if isinstance(v, list) else [v] for v in d.values()]
    swept = [k for k, v in d.items() if isinstance(v, list)]
    return [dict(zip(keys, combo)) for combo in itertools.product(*vals)], swept


def dataset_files(params, data_dir):
    key = json.dumps(params, sort_keys=True)
    h = hashlib.sha1(key.encode()).hexdigest()[:10]
    prefix = data_dir / f"ds-{h}"
    return prefix, key


def ensure_dataset(params, bin_dir, data_dir, dry):
    prefix, key = dataset_files(params, data_dir)
    if Path(f"{prefix}.bin").exists() and Path(f"{prefix}.pat").exists():
        return prefix
    cmd = [str(bin_dir / "sigscan-gen"), "--out", str(prefix)]
    for k, v in params.items():
        if k in ("no-match",):
            if v:
                cmd.append(f"--{k}")
        elif v is not None:
            cmd += [f"--{k}", str(v)]
    print("  gen", " ".join(cmd[3:]), flush=True)
    if not dry:
        data_dir.mkdir(parents=True, exist_ok=True)
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        with open(data_dir / "index.jsonl", "a") as f:
            f.write(json.dumps({"prefix": prefix.name, "params": json.loads(key)}) + "\n")
    return prefix


def tag_of(items):
    return ";".join(f"{k}={v}" for k, v in items)


def build_jobs(cfg):
    ds_base = dict(BASE_DATASET, **cfg.get("dataset", {}))
    matrix = cfg.get("matrix", {})
    if isinstance(matrix, list):
        ds_points = [dict(ds_base, **m) for m in matrix]
        ds_swept = list(dict.fromkeys(k for m in matrix for k in m))
    else:
        ds_points, ds_swept = product(dict(ds_base, **matrix))
    jobs = []
    for ds in ds_points:
        for run in cfg["runs"]:
            if "max_size" in run and parse_size(ds["size"]) > parse_size(run["max_size"]):
                continue
            if "only" in run and any(ds.get(k) not in (v if isinstance(v, list) else [v])
                                     for k, v in run["only"].items()):
                continue
            arg_points, arg_swept = product(dict(run.get("args", {}), **({"np": run["np"]} if "np" in run else {})))
            for a in arg_points:
                tag = tag_of([(k, ds.get(k, "-")) for k in ds_swept] + [(k, a[k]) for k in arg_swept])
                jobs.append((ds, run, a, tag))
    return jobs


def command(run, args, prefix, cfg, bin_dir, mpirun):
    impl = run["impl"]
    exe = str(bin_dir / f"sigscan-{impl}")
    cmd = []
    if impl == "mpi":
        cmd = [mpirun, "-np", str(args["np"])] + run.get("mpi_flags", [])
    cmd += [exe, "--text", f"{prefix}.bin", "--patterns", f"{prefix}.pat"]
    if run.get("stats"):
        return cmd + ["--stats"]
    for k, v in args.items():
        if k == "np":
            continue
        cmd += [f"--{k}"] if v is True else [f"--{k}", str(v)]
    cmd += ["--mode", run.get("mode", cfg.get("mode", "count")),
            "--reps", str(run.get("reps", cfg.get("reps", 7))),
            "--warmup", str(run.get("warmup", cfg.get("warmup", 1))),
            "--csv", "--csv-header", "--quiet"]
    return cmd


def label_of(run):
    return run.get("label", run["impl"]) + ("-stats" if run.get("stats") else "")


def append_rows(path, stdout, exp, label, tag):
    lines = [l for l in stdout.splitlines() if l.strip()]
    if not lines:
        return 0
    header, rows = lines[0], lines[1:]
    new = not path.exists()
    with open(path, "a") as f:
        if new:
            f.write(header + ",exp,label,tag\n")
        for r in rows:
            f.write(f"{r},{exp},{label},\"{tag}\"\n")
    return len(rows)


def done_keys(*paths):
    keys = set()
    for path in paths:
        if path.exists():
            with open(path) as f:
                keys |= {(r["label"], r["tag"]) for r in csv.DictReader(f)}
    return keys


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("configs", nargs="+", type=Path, help="bench/configs/*.json")
    ap.add_argument("--bin", type=Path, default=ROOT / "build")
    ap.add_argument("--data", type=Path, default=ROOT / "data")
    ap.add_argument("--out", type=Path, default=ROOT / "results" / "raw")
    ap.add_argument("--mpirun", default=shutil.which("mpirun") or "mpirun")
    ap.add_argument("--dry-run", action="store_true", help="print the commands only")
    ap.add_argument("--resume", action="store_true", help="skip runs whose tag is already in the CSV")
    ap.add_argument("--fresh", action="store_true", help="delete the experiment CSV before running")
    ap.add_argument("--cooldown", type=float, default=2.0, help="seconds of idle time between runs")
    ap.add_argument("--seed", type=int, default=1, help="seed of the run-order shuffle")
    args = ap.parse_args()

    env = dict(os.environ, TMPDIR=os.environ.get("TMPDIR", "/tmp"))
    args.out.mkdir(parents=True, exist_ok=True)
    for cfg_path in args.configs:
        cfg = json.loads(cfg_path.read_text())
        exp = cfg["name"]
        out = args.out / f"{exp}.csv"
        out_stats = args.out / f"{exp}_stats.csv"
        if args.fresh and not args.dry_run:
            for p in (out, out_stats):
                p.unlink(missing_ok=True)
        jobs = build_jobs(cfg)
        if args.resume:
            done = done_keys(out, out_stats)
            jobs = [j for j in jobs if (label_of(j[1]), j[3]) not in done]
        random.Random(args.seed).shuffle(jobs)
        datasets = {json.dumps(j[0], sort_keys=True): j[0] for j in jobs}
        print(f"[{exp}] {len(jobs)} runs, {len(datasets)} datasets", flush=True)
        prefixes = {k: ensure_dataset(ds, args.bin, args.data, args.dry_run) for k, ds in datasets.items()}
        t0 = time.time()
        for n, (ds, run, a, tag) in enumerate(jobs, 1):
            prefix = prefixes[json.dumps(ds, sort_keys=True)]
            cmd = command(run, a, prefix, cfg, args.bin, args.mpirun)
            label = label_of(run)
            print(f"  [{n}/{len(jobs)}] {label} {tag}", flush=True)
            if args.dry_run:
                print("    " + " ".join(cmd))
                continue
            res = subprocess.run(cmd, capture_output=True, text=True, env=env)
            if res.returncode != 0:
                sys.exit(f"FAILED ({res.returncode}): {' '.join(cmd)}\n{res.stderr}")
            append_rows(out_stats if run.get("stats") else out, res.stdout, exp, label, tag)
            if args.cooldown:
                time.sleep(args.cooldown)
        print(f"[{exp}] done in {time.time() - t0:.0f} s -> {out}", flush=True)


if __name__ == "__main__":
    main()
