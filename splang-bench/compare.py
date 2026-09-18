#!/usr/bin/env python3
"""Check the ParlayLib ports against splang's verified measures.

V1  the measures are properties of the computation graph, not of the schedule,
    so they must not move as the worker count changes.
V2  they must equal what splang's checker computes, up to a known per-example
    scale factor. allocfree allocates through space_alloc and matches exactly;
    nqueens allocates parlay::sequence boards, and sequence prepends a capacity
    word to each buffer, so every measure comes out scaled by (n+1)/n.
V3  the footprint the run actually reached must obey footprint <= P * R1. This
    one IS schedule-dependent, so it is checked against the bound rather than
    compared across worker counts.

Usage:  python3 compare.py [--splang PATH] [--threads 1,2,8] [--quick]
"""

import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SPLANG = os.path.expanduser("~/repos/splang/.lake/build/bin/splang")

# splang's default fuel is too small for the larger allocfree runs.
FUEL = "100000000000"

FIELDS = ("delta", "r1", "rinf")

# Numerator and denominator by which this port's measures differ from splang's,
# as a function of the size. nqueens boards are n-cell arrays held in a
# parlay::sequence, which stores a size_t capacity alongside them.
SCALE = {
    "allocfree": lambda n: (1, 1),
    "nqueens": lambda n: (n + 1, n),
}

CASES = {
    "allocfree": [16, 256, 3000],
    "nqueens": [5, 6, 7, 8],
}
QUICK = {
    "allocfree": [16],
    "nqueens": [5, 8],
}


def run_json(argv, env=None):
    out = subprocess.run(argv, capture_output=True, text=True, env=env)
    if out.returncode != 0:
        sys.exit("command failed: %s\n%s%s" % (" ".join(argv), out.stdout, out.stderr))
    return json.loads(out.stdout.strip().splitlines()[-1])


def splang_reference(splang, example, size):
    return run_json([splang, example, "--size", str(size), "--json", "--fuel", FUEL])


def parlay_run(example, size, threads):
    env = dict(os.environ, PARLAY_NUM_THREADS=str(threads))
    return run_json([os.path.join(HERE, example), "--size", str(size), "--json"], env=env)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--splang", default=os.environ.get("SPLANG", DEFAULT_SPLANG))
    ap.add_argument("--threads", default="1,2,3,8,16,64")
    ap.add_argument("--quick", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.splang):
        sys.exit("splang binary not found at %s (build it with `lake build`, "
                 "or pass --splang PATH)" % args.splang)

    threads = [int(t) for t in args.threads.split(",")]
    cases = QUICK if args.quick else CASES

    print("%-10s %6s %7s %8s %10s %12s %11s %11s   %s" %
          ("example", "size", "threads", "value", "R1", "Rinf",
           "footprint", "P*R1", "status"))

    failures = 0
    for example, sizes in cases.items():
        for size in sizes:
            ref = splang_reference(args.splang, example, size)
            for t in threads:
                got = parlay_run(example, size, t)
                num, den = SCALE[example](size)
                want = {f: ref[f] * num // den for f in FIELDS}
                want["value"] = ref["value"]
                bad = [f for f in ("value",) + FIELDS if got[f] != want[f]]
                if bad:
                    failures += 1
                    detail = "FAIL " + " ".join(
                        "%s=%s want %s" % (f, got[f], want[f]) for f in bad)
                elif not got["within_bound"]:
                    failures += 1
                    detail = "FAIL footprint %d > P*R1 %d" % (got["footprint"], got["bound"])
                else:
                    detail = "ok"
                print("%-10s %6d %7d %8s %10d %12d %11d %11d   %s" %
                      (example, size, t, got["value"], got["r1"], got["rinf"],
                       got["footprint"], got["bound"], detail))

    print()
    if failures:
        print("%d mismatch(es)" % failures)
        return 1
    print("all runs agree with splang and are invariant across %s workers; "
          "every footprint is within P*R1" % ",".join(str(t) for t in threads))
    return 0


if __name__ == "__main__":
    sys.exit(main())
