#!/usr/bin/env python3
"""Check the ParlayLib ports against splang's verified measures.

V1  the measures are properties of the computation graph, not of the schedule,
    so they must not move as the worker count changes.
V2  they must equal what splang's checker computes, exactly: delta, R1star,
    Rinf, S and R1star_partial. Every port holds its arrays in a
    parlay::sequence, whose capacity word stands in for splang's array header
    cell, so no scale factor is needed; a port that needs one is not modelling
    splang's cost model and should be fixed. The value must match too, except
    for strassen, whose operands differ between the two sides; there each side
    checks its own product (splang against Mathlib, the port by Freivalds).
V3  the footprint the run actually reached must obey footprint <= P * R1star
    and, when it applies, footprint <= S + P * R1star_partial. This one IS
    schedule-dependent, so it is checked against the bounds rather than
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

FIELDS = ("delta", "r1", "rinf", "s", "r1star_partial")

# Examples whose returned value means the same thing on both sides.
SAME_VALUE = {"allocfree", "nqueens"}

CASES = {
    "allocfree": [16, 256, 3000],
    "nqueens": [5, 6, 7, 8],
    "strassen": [16, 32, 64],
}
QUICK = {
    "allocfree": [16],
    "nqueens": [5, 8],
    "strassen": [32],
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

    print("%-10s %5s %7s %8s %9s %8s %8s %9s %9s %9s   %s" %
          ("example", "size", "threads", "R1star", "Rinf", "S", "R1*part",
           "footprint", "P*R1star", "S+P*R1*p", "status"))

    failures = 0
    for example, sizes in cases.items():
        for size in sizes:
            ref = splang_reference(args.splang, example, size)
            if example not in SAME_VALUE and not (ref["check"] or "").startswith("all "):
                sys.exit("splang's %s %d is wrong: %s" % (example, size, ref["check"]))
            compared = (("value",) if example in SAME_VALUE else ()) + FIELDS
            for t in threads:
                got = parlay_run(example, size, t)
                bad = [f for f in compared if got[f] != ref[f]]
                partial = "%d" % got["partial_bound"] if got["partial_applies"] else "n/a"
                if bad:
                    failures += 1
                    detail = "FAIL " + " ".join(
                        "%s=%s want %s" % (f, got[f], ref[f]) for f in bad)
                elif not got["within_bound"]:
                    failures += 1
                    detail = "FAIL footprint %d > P*R1star %d" % (got["footprint"], got["bound"])
                elif got["partial_applies"] and not got["within_partial_bound"]:
                    failures += 1
                    detail = "FAIL footprint %d > S+P*R1star_partial %s" % (got["footprint"], partial)
                else:
                    detail = "ok"
                print("%-10s %5d %7d %8d %9d %8d %8d %9d %9d %9s   %s" %
                      (example, size, t, got["r1"], got["rinf"], got["s"],
                       got["r1star_partial"], got["footprint"], got["bound"], partial, detail))

    print()
    if failures:
        print("%d mismatch(es)" % failures)
        return 1
    print("all runs agree with splang and are invariant across %s workers; every footprint "
          "is within P*R1star and S+P*R1star_partial" % ",".join(str(t) for t in threads))
    return 0


if __name__ == "__main__":
    sys.exit(main())
