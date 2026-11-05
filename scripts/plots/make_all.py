#!/usr/bin/env python3
"""Rebuilds all figures and tables of the report from results/raw."""
import importlib
import sys
import traceback
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

MODULES = ["plot_e1_size", "plot_e2_npat", "plot_e3_e4", "plot_e5_omp", "plot_e6_mpi", "plot_e7_chunk",
           "plot_e8b_interleave", "plot_gpu", "plot_e10_grid", "tables"]


def main():
    failed = 0
    for name in sys.argv[1:] or MODULES:
        print(name)
        try:
            mod = importlib.import_module(name)
            (mod.main if hasattr(mod, "main") else mod.best_results)()
        except FileNotFoundError as e:
            print(f"  skipped: {Path(e.filename).name} not measured yet")
        except Exception:
            failed += 1
            traceback.print_exc()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
