#!/usr/bin/env python3
"""Integration test: every built version must give exactly the same matches as the reference,
and the reference must contain every hit injected by the generator."""
import argparse
import csv
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

SCENARIOS = {
    # name: generator arguments
    "basic": "--size 2M --npat 300 --minlen 4 --maxlen 24 --hit-rate 200 --partial-rate 200 --prefix-share 0.4",
    "boundary": "--size 1M --npat 100 --minlen 8 --maxlen 64 --hit-rate 0 --boundary-block 4k",
    "nomatch": "--size 1M --npat 500 --minlen 4 --maxlen 16 --no-match",
    "dense-hits": "--size 512k --npat 20 --minlen 1 --maxlen 6 --alphabet 3 --hit-rate 2000",
    "long-patterns": "--size 4k --npat 10 --minlen 100 --maxlen 600 --hit-rate 300",
    "dna": "--size 1M --npat 1000 --minlen 6 --maxlen 20 --alphabet dna --hit-rate 100 --prefix-share 0.8",
}


MPIEXEC_TEARDOWN = "unable to tear down the job tree"


def run(cmd, env=None):
    for attempt in range(5):
        if attempt:
            time.sleep(2)
        res = subprocess.run(cmd, capture_output=True, text=True, env=env)
        if res.returncode == 0 or MPIEXEC_TEARDOWN not in res.stdout + res.stderr:
            break
    if res.returncode != 0:
        sys.exit(f"FAILED: {' '.join(map(str, cmd))}\n{res.stderr}")
    return res.stdout


def read_matches(path):
    with open(path) as f:
        rows = csv.reader(f)
        next(rows)
        return {(int(p), int(o)) for p, o in rows}


def count_line(stdout):
    rec = next(csv.DictReader(stdout.splitlines()))
    return int(rec["matches"]), rec["checksum"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True, type=Path)
    ap.add_argument("--work", required=True, type=Path)
    ap.add_argument("--mpirun", default=shutil.which("mpirun") or shutil.which("mpiexec") or "mpirun")
    args = ap.parse_args()
    b = args.bin
    exe = ".exe" if os.name == "nt" else ""
    gen, seq, omp, mpi, cuda = (b / f"sigscan-{x}{exe}" for x in ("gen", "seq", "omp", "mpi", "cuda"))
    args.work.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    if os.name != "nt":
        env["TMPDIR"] = os.environ.get("TMPDIR", "/tmp")
    oversub = [] if os.name == "nt" else ["--oversubscribe"]

    count_only = set()
    variants = []  # (label, command)
    variants += [(f"seq-{l}", [seq, "--layout", l]) for l in ("dense", "compressed")]
    variants += [("seq-pfac-cpu", [seq, "--algo", "pfac"])]
    variants += [(f"seq-il{k}", [seq, "--interleave", str(k)]) for k in (2, 4, 8, 16)]
    variants += [("seq-il4-compressed", [seq, "--interleave", "4", "--layout", "compressed"])]
    if omp.exists():
        for p in (1, 2, 3, 7, 10):
            variants.append((f"omp-blocks-p{p}", [omp, "--threads", str(p), "--schedule", "blocks"]))
        for sched in ("static", "dynamic", "guided"):
            for chunk in ("1k", "3", "64k"):
                variants.append((f"omp-{sched}-{chunk}", [omp, "--threads", "4", "--schedule", sched, "--chunk", chunk]))
        variants.append(("omp-blocks-il8", [omp, "--threads", "3", "--interleave", "8"]))
        variants.append(("omp-dynamic-il4-tiny", [omp, "--threads", "4", "--schedule", "dynamic", "--chunk", "37",
                                                  "--interleave", "4"]))
        variants.append(("omp-dynamic-il16", [omp, "--threads", "4", "--schedule", "dynamic", "--chunk", "5k",
                                              "--interleave", "16"]))
        variants.append(("omp-compressed", [omp, "--threads", "5", "--schedule", "dynamic", "--chunk", "7k",
                                            "--layout", "compressed"]))
    if mpi.exists():
        for np_ in (1, 2, 3, 4, 7):
            variants.append((f"mpi-np{np_}", [args.mpirun, "-np", str(np_), *oversub, mpi]))

    if cuda.exists():
        for k in ("global", "ldg", "smem-text", "smem-root", "smem-all"):
            variants.append((f"cuda-{k}", [cuda, "--kernel", k, "--block", "128", "--grid-mult", "2"]))
        # pipeline mode has count mode only
        variants.append(("cuda-pipeline", [cuda, "--streams", "3", "--chunk", "100k"]))
        variants.append(("cuda-pipeline-tiny", [cuda, "--streams", "2", "--chunk", "1k"]))
    count_only = {"cuda-pipeline", "cuda-pipeline-tiny"}

    failures = 0
    for name, gargs in SCENARIOS.items():
        prefix = args.work / name
        run([gen, "--out", prefix, "--seed", "99", *gargs.split()])
        text, pats = f"{prefix}.bin", f"{prefix}.pat"
        ref_out = f"{prefix}.ref.csv"
        run([seq, "--text", text, "--patterns", pats, "--mode", "list", "--out", ref_out, "--quiet"])
        ref = read_matches(ref_out)
        truth = read_matches(f"{prefix}.truth.csv")
        ok = truth <= ref
        if name == "nomatch":
            ok &= len(ref) == 0
        print(f"[{name}] reference: {len(ref)} matches, truth {len(truth)} {'OK' if ok else 'MISSING TRUTH'}")
        failures += not ok
        ref_count = None
        for label, cmd in variants:
            if label in count_only:
                got = ref
            else:
                out = f"{prefix}.{label}.csv"
                run([*cmd, "--text", text, "--patterns", pats, "--mode", "list", "--out", out, "--quiet"], env)
                got = read_matches(out)
            cnt = count_line(run([*cmd, "--text", text, "--patterns", pats, "--mode", "count", "--csv",
                                  "--csv-header", "--quiet"], env))
            ref_count = ref_count or cnt
            good = got == ref and cnt == ref_count and cnt[0] == len(ref)
            if not good:
                failures += 1
                print(f"  FAIL {label}: list {len(got)} (missing {len(ref - got)}, extra {len(got - ref)}), "
                      f"count {cnt} vs {ref_count}")
        print(f"  {len(variants)} variants checked")
    print("ALL OK" if failures == 0 else f"{failures} FAILURE(S)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
