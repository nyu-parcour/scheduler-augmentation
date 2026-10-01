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

Stopgap until the splang submodule reaches the commit these ports target
(highwater_spine, the current strassenDemo, AllocFree.lean): a splang that
reports no S and R1star_partial predates both, so V2 then compares allocfree
and nqueens on the other fields only, and checks S, R1star_partial and all of
strassen for V1 alone, against the first worker count. A warning says so.

par-clique has no splang counterpart, so it gets V1 and V3 only. V1 is checked
across worker counts and across repeated runs at each count, and covers the
count as well as the measures. V3 relies on the busy-leaves property, which
parlay's default join (steal while waiting) does not preserve: it is a hard
check under --join wait, and under --join steal violations are reported but do
not fail.

Usage:  python3 compare.py [--splang PATH] [--threads 1,2,8] [--quick]
                           [--clique-threads 1,2,10] [--repeats N]
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

# The fields a splang from before highwater_spine lacks, and the examples whose
# splang counterpart changed along with it (see the stopgap above).
SPINE_FIELDS = ("s", "r1star_partial")
SPINE_EXAMPLES = {"strassen"}

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

# par-clique cases: (graph arguments, k, extra flags). Each runs under both
# join policies. The graphs are par-clique's built-in seeded RMAT, so no files
# are needed.
CLIQUE_JOINS = ("steal", "wait")
CLIQUE_FIELDS = ("value", "delta", "r1", "rinf", "r1_lr")
CLIQUE_CASES = [
    (["--rmat", "256,4000,1"], 4, []),
    (["--rmat", "256,4000,1"], 4, ["--prune", "on"]),
    (["--rmat", "256,4000,1"], 4, ["--id", "i64"]),
    (["--rmat", "256,4000,1"], 4, ["--early-base", "off"]),
    (["--rmat", "256,4000,1"], 4, ["--grain", "4"]),
    (["--rmat", "1024,20000,2"], 5, []),
    (["--rmat", "4096,60000,3"], 4, []),
    (["--rmat", "4096,60000,3"], 6, []),
]
CLIQUE_QUICK = CLIQUE_CASES[:1] + CLIQUE_CASES[5:6]


def run_json(argv, env=None):
    out = subprocess.run(argv, capture_output=True, text=True, env=env)
    if out.returncode != 0:
        sys.exit("command failed: %s\n%s%s" % (" ".join(argv), out.stdout, out.stderr))
    return json.loads(out.stdout.strip().splitlines()[-1])


def splang_reference(splang, example, size):
    return run_json([splang, example, "--size", str(size), "--json", "--fuel", FUEL])


def splang_commit(splang):
    """The commit the splang binary was built from, or its path if unknown."""
    out = subprocess.run(["git", "-C", os.path.dirname(os.path.abspath(splang)),
                          "rev-parse", "--short", "HEAD"], capture_output=True, text=True)
    return out.stdout.strip() if out.returncode == 0 else splang


def parlay_run(example, size, threads):
    env = dict(os.environ, PARLAY_NUM_THREADS=str(threads))
    return run_json([os.path.join(HERE, example), "--size", str(size), "--json"], env=env)


def clique_run(graph, k, flags, threads):
    env = dict(os.environ, PARLAY_NUM_THREADS=str(threads))
    argv = [os.path.join(HERE, "par-clique")] + graph + ["--k", str(k), "--json"] + flags
    return run_json(argv, env=env)


def check_par_clique(cases, threads, repeats):
    """V1 and V3 for par-clique. Returns the number of failures."""
    print()
    print("%-34s %6s %7s %10s %8s %12s %9s   %s" %
          ("par-clique case", "join", "threads", "value", "R1", "Rinf",
           "fp/P*R1", "status"))
    failures = 0
    steal_violations = steal_runs = 0
    for graph, k, flags in cases:
        name = " ".join(graph[1:] + ["k=%d" % k] + flags)
        for join in CLIQUE_JOINS:
            ref = None
            for t in threads:
                runs = [clique_run(graph, k, flags + ["--join", join], t)
                        for _ in range(repeats)]
                if ref is None:
                    ref = runs[0]
                moved = sorted({f for r in runs for f in CLIQUE_FIELDS if r[f] != ref[f]})
                over = [r for r in runs if not r["within_bound"]]
                worst = max(r["footprint_bytes"] / (r["threads"] * r["s1star_bytes"])
                            for r in runs)
                if moved:
                    failures += 1
                    detail = "FAIL V1 " + " ".join(
                        "%s=%s want %s" % (f, next(r[f] for r in runs if r[f] != ref[f]), ref[f])
                        for f in moved)
                elif over and join == "wait":
                    failures += 1
                    detail = "FAIL V3 %d/%d runs exceed P*R1" % (len(over), len(runs))
                elif over:
                    detail = "ok (V3: %d/%d over, not enforced)" % (len(over), len(runs))
                else:
                    detail = "ok"
                if join == "steal":
                    steal_violations += len(over)
                    steal_runs += len(runs)
                print("%-34s %6s %7d %10s %8d %12d %9.2f   %s" %
                      (name, join, t, ref["value"], ref["r1"], ref["rinf"], worst, detail))
    print()
    print("par-clique: V1 over %s workers x %d repeats; --join steal exceeded P*R1 in "
          "%d/%d runs" % (",".join(str(t) for t in threads), repeats,
                          steal_violations, steal_runs))
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--splang", default=os.environ.get("SPLANG", DEFAULT_SPLANG))
    ap.add_argument("--threads", default="1,2,3,8,16,64")
    ap.add_argument("--clique-threads", default="1,2,3,8,10,16")
    ap.add_argument("--repeats", type=int, default=3)
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
    partial_v2 = False
    for example, sizes in cases.items():
        for size in sizes:
            ref = splang_reference(args.splang, example, size)
            full = all(f in ref for f in SPINE_FIELDS)
            if full and example not in SAME_VALUE and not (ref["check"] or "").startswith("all "):
                sys.exit("splang's %s %d is wrong: %s" % (example, size, ref["check"]))
            compared = (("value",) if example in SAME_VALUE else ()) + FIELDS
            if not full:
                partial_v2 = True
                compared = () if example in SPINE_EXAMPLES else tuple(
                    f for f in compared if f not in SPINE_FIELDS)
            first = None
            for t in threads:
                got = parlay_run(example, size, t)
                first = first or got
                bad = [f for f in compared if got[f] != ref[f]]
                moved = [f for f in FIELDS if f not in compared and got[f] != first[f]]
                partial = "%d" % got["partial_bound"] if got["partial_applies"] else "n/a"
                if bad:
                    failures += 1
                    detail = "FAIL " + " ".join(
                        "%s=%s want %s" % (f, got[f], ref[f]) for f in bad)
                elif moved:
                    failures += 1
                    detail = "FAIL V1 " + " ".join(
                        "%s=%s want %s" % (f, got[f], first[f]) for f in moved)
                elif not got["within_bound"]:
                    failures += 1
                    detail = "FAIL footprint %d > P*R1star %d" % (got["footprint"], got["bound"])
                elif got["partial_applies"] and not got["within_partial_bound"]:
                    failures += 1
                    detail = "FAIL footprint %d > S+P*R1star_partial %s" % (got["footprint"], partial)
                elif not compared:
                    detail = "ok (no V2)"
                elif len(compared) < len(FIELDS) + (example in SAME_VALUE):
                    detail = "ok (V2 partial)"
                else:
                    detail = "ok"
                print("%-10s %5d %7d %8d %9d %8d %8d %9d %9d %9s   %s" %
                      (example, size, t, got["r1"], got["rinf"], got["s"],
                       got["r1star_partial"], got["footprint"], got["bound"], partial, detail))

    print()
    if failures:
        print("%d mismatch(es)" % failures)
    else:
        print("all runs agree with splang%s and are invariant across %s workers; every "
              "footprint is within P*R1star and S+P*R1star_partial"
              % (" where compared" if partial_v2 else "", ",".join(str(t) for t in threads)))
    if partial_v2:
        print("WARNING: V2 partially skipped: splang %s lacks %s (highwater_spine); "
              "those fields and %s are checked for V1 only"
              % (splang_commit(args.splang), " and ".join(SPINE_FIELDS),
                 ", ".join(sorted(SPINE_EXAMPLES))))

    clique_threads = [int(t) for t in args.clique_threads.split(",")]
    clique_failures = check_par_clique(CLIQUE_QUICK if args.quick else CLIQUE_CASES,
                                       clique_threads, args.repeats)
    if clique_failures:
        print("par-clique: %d failure(s)" % clique_failures)
    return 1 if failures or clique_failures else 0


if __name__ == "__main__":
    sys.exit(main())
