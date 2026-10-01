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
count as well as the measures. V3 checks both bounds, and relies on the
busy-leaves property, which parlay's default join (steal while waiting) does
not preserve: it is a hard check under --join wait, and under --join steal
violations are reported but do not fail. Every run must also have
R1(LR) <= R* <= Rinf, and at P = 1 a footprint of exactly R1(LR).

Each par-clique ablation is also checked against the same case without it, on
the schedule-independent measures. Every ablation keeps the count. The
charging ablations do the same computation and only charge more: --T all adds
exactly the top-level T (8n + 8 bytes) and --charge-graph exactly the graph
(8n + 4 m_oriented + 24 bytes) to R1(LR), R*, Rinf and S, leaving delta and
R1star_partial alone, and --T none never charges more than --T inner. Under
--order kcore the max out-degree is at most the degeneracy.

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
CLIQUE_FIELDS = ("value", "delta", "r1", "rinf", "r1_lr", "s", "r1star_partial",
                 "m_oriented", "max_out_degree")
ALL_ABLATIONS = ["--variant", "induced", "--T", "all", "--charge-graph", "on",
                 "--order", "kcore"]
CLIQUE_CASES = [
    (["--rmat", "256,4000,1"], 4, []),
    (["--rmat", "256,4000,1"], 4, ["--prune", "on"]),
    (["--rmat", "256,4000,1"], 4, ["--id", "i64"]),
    (["--rmat", "256,4000,1"], 4, ["--early-base", "off"]),
    (["--rmat", "256,4000,1"], 4, ["--grain", "4"]),
    (["--rmat", "256,4000,1"], 4, ["--T", "none"]),
    (["--rmat", "256,4000,1"], 4, ["--T", "all"]),
    (["--rmat", "256,4000,1"], 4, ["--charge-graph", "on"]),
    (["--rmat", "256,4000,1"], 4, ["--order", "kcore"]),
    (["--rmat", "256,4000,1"], 4, ["--variant", "induced"]),
    (["--rmat", "256,4000,1"], 4, ALL_ABLATIONS),
    (["--rmat", "1024,20000,2"], 5, []),
    (["--rmat", "1024,20000,2"], 5, ["--variant", "induced"]),
    (["--rmat", "4096,60000,3"], 4, []),
    (["--rmat", "4096,60000,3"], 6, []),
    (["--rmat", "4096,60000,3"], 6, ["--variant", "induced"]),
    (["--rmat", "4096,60000,3"], 6, ["--order", "kcore"]),
]
CLIQUE_QUICK = [CLIQUE_CASES[0], CLIQUE_CASES[10], CLIQUE_CASES[11]]

# The measures, in bytes, that the ablation checks compare; the first four are
# the ones a charging ablation shifts.
SHIFTED = ("s1_bytes", "s1star_bytes", "sinf_bytes", "s_bytes")
MEASURE_BYTES = SHIFTED + ("r1star_partial_bytes", "gross_bytes")


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


def with_flag(flags, flag, value):
    """flags with flag's value set to value, or with flag removed if None."""
    out, i = [], 0
    while i < len(flags):
        if flags[i] == flag:
            i += 2
        else:
            out.append(flags[i])
            i += 1
    return out + ([flag, value] if value is not None else [])


def flag_value(flags, flag):
    return flags[flags.index(flag) + 1] if flag in flags else None


def check_ablations(flags, run):
    """The schedule-independent checks of each ablation in flags against the
    same case without it. run(flags) gives one run of the case with those
    flags. Returns [(baseline flags, what is checked, problems)]."""
    got = run(flags)
    results = []

    def against(base_flags, what, check):
        base = run(base_flags)
        problems = [] if got["value"] == base["value"] else [
            "value %s want %s" % (got["value"], base["value"])]
        results.append((base_flags, what, problems + check(base)))

    def shifted_by(extra):
        def check(base):
            want = {f: base[f] + (extra if f in SHIFTED else 0) for f in MEASURE_BYTES}
            return ["%s %+d want %+d" % (f, got[f] - base[f], want[f] - base[f])
                    for f in MEASURE_BYTES if got[f] != want[f]]
        return check

    def at_most(base):
        return ["%s %d > %d" % (f, got[f], base[f]) for f in MEASURE_BYTES if got[f] > base[f]]

    def same_count(base):
        return []

    if flag_value(flags, "--T") == "all":
        against(with_flag(flags, "--T", "inner"), "+8n+8 B on R1(LR),R*,Rinf,S",
                shifted_by(8 * got["n"] + 8))
    if flag_value(flags, "--T") == "none":
        against(with_flag(flags, "--T", "inner"), "no measure above it", at_most)
    if flag_value(flags, "--charge-graph") == "on":
        against(with_flag(flags, "--charge-graph", None), "+8n+4m'+24 B on R1(LR),R*,Rinf,S",
                shifted_by(8 * got["n"] + 4 * got["m_oriented"] + 24))
    if flag_value(flags, "--order") == "kcore":
        against(with_flag(flags, "--order", None), "same count; D <= degeneracy",
                lambda base: [] if got["max_out_degree"] <= got["degeneracy"] else [
                    "D %d > degeneracy %d" % (got["max_out_degree"], got["degeneracy"])])
    if flag_value(flags, "--variant") == "induced":
        against(with_flag(flags, "--variant", None), "same count", same_count)
    return results


def check_par_clique(cases, threads, repeats):
    """V1 and V3 for par-clique, and the ablation checks. Returns the number
    of failures."""
    names = [" ".join(graph[1:] + ["k=%d" % k] + flags) for graph, k, flags in cases]
    width = max(len(n) for n in names + ["par-clique case"])
    print()
    print("%-*s %6s %7s %10s %8s %12s %9s %9s   %s" %
          (width, "par-clique case", "join", "threads", "value", "R1", "Rinf",
           "fp/P*R1", "fp/S+PR1p", "status"))
    failures = 0
    steal_over = steal_over_partial = steal_runs = 0
    for name, (graph, k, flags) in zip(names, cases):
        for join in CLIQUE_JOINS:
            ref = None
            for t in threads:
                runs = [clique_run(graph, k, flags + ["--join", join], t)
                        for _ in range(repeats)]
                if ref is None:
                    ref = runs[0]
                moved = sorted({f for r in runs for f in CLIQUE_FIELDS if r[f] != ref[f]})
                unordered = [r for r in runs
                             if not r["s1_bytes"] <= r["s1star_bytes"] <= r["sinf_bytes"]]
                serial = [r for r in runs
                          if r["threads"] == 1 and r["footprint_bytes"] != r["s1_bytes"]]
                over = [r for r in runs if not r["within_bound"]]
                over_partial = [r for r in runs
                                if r["partial_applies"] and not r["within_partial_bound"]]
                worst = max(r["footprint_bytes"] / (r["threads"] * r["s1star_bytes"])
                            for r in runs)
                worst_partial = max(r["footprint_bytes"] /
                                    (r["s_bytes"] + r["threads"] * r["r1star_partial_bytes"])
                                    for r in runs)
                exceeded = "%d/%d over P*R1, %d/%d over S+P*R1p" % (
                    len(over), len(runs), len(over_partial), len(runs))
                if moved:
                    failures += 1
                    detail = "FAIL V1 " + " ".join(
                        "%s=%s want %s" % (f, next(r[f] for r in runs if r[f] != ref[f]), ref[f])
                        for f in moved)
                elif unordered:
                    failures += 1
                    detail = "FAIL R1(LR) <= R* <= Rinf does not hold"
                elif serial:
                    failures += 1
                    detail = "FAIL footprint %d B != R1(LR) %d B at P = 1" % (
                        serial[0]["footprint_bytes"], serial[0]["s1_bytes"])
                elif (over or over_partial) and join == "wait":
                    failures += 1
                    detail = "FAIL V3 " + exceeded
                elif over or over_partial:
                    detail = "ok (V3: %s, not enforced)" % exceeded
                else:
                    detail = "ok"
                if join == "steal":
                    steal_over += len(over)
                    steal_over_partial += len(over_partial)
                    steal_runs += len(runs)
                print("%-*s %6s %7d %10s %8d %12d %9.2f %9.2f   %s" %
                      (width, name, join, t, ref["value"], ref["r1"], ref["rinf"], worst,
                       worst_partial, detail))
    print()
    print("par-clique: V1 over %s workers x %d repeats; --join steal exceeded P*R1 in "
          "%d/%d runs and S+P*R1star_partial in %d/%d" % (
              ",".join(str(t) for t in threads), repeats, steal_over, steal_runs,
              steal_over_partial, steal_runs))

    print()
    print("%-*s   %-*s   %s" % (width, "par-clique ablation", width, "against", "check"))
    for name, (graph, k, flags) in zip(names, cases):
        cache = {}

        def run(fl, graph=graph, k=k, cache=cache):
            key = tuple(fl)
            if key not in cache:
                cache[key] = clique_run(graph, k, fl, 1)
            return cache[key]

        for base_flags, what, problems in check_ablations(flags, run):
            failures += bool(problems)
            base = " ".join(graph[1:] + ["k=%d" % k] + base_flags)
            print("%-*s   %-*s   %s: %s" % (width, name, width, base, what,
                                            "FAIL " + "; ".join(problems) if problems else "ok"))
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
