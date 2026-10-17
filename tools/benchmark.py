#!/usr/bin/env python3
"""Measure cctxpack's token estimator against a real tokenizer.

Why this exists
---------------
The estimator is a heuristic, so its accuracy has to be measured rather than
asserted.  Two rules learned the hard way are baked in:

1.  The estimates come from the REAL BINARY (`cctxpack count --format json`),
    never from a Python reimplementation of the classifier.  An earlier
    investigation rebuilt the classifier in Python and reported a worst case of
    -54% that the shipped binary does not reproduce; it diverged from the real
    code on one file.  Two of its error statistics matched exactly, which is
    what made the wrong number look plausible.

2.  Binary files are excluded.  cctxpack does not estimate them - it emits a
    placeholder carrying the path and byte size - so including them invents
    worst cases that the tool never produces.  An uncorrected harness scored a
    __pycache__/*.pyc at -61% and a blob.bin at -54%.

What is reported
----------------
Aggregate error AND the per-file distribution.  Aggregate alone is not an
acceptance metric: per-file errors are close to zero-mean and cancel across a
corpus, so a large aggregate error can coexist with small per-file error, and
a small aggregate can hide a bad tail.  Both are printed, always.

Reproducing the published numbers
---------------------------------
    python tools/benchmark.py \\
        "C:/Old projects/sniff" \\
        "C:/New projects/C projects/cctxpack" \\
        "C:/Users/jdroq/.agency-agents"

Writes benchmark-results.json and benchmark-results.md next to this file.
"""
import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def find_binary(explicit):
    if explicit:
        return explicit
    for rel in ("build-rel/Release/cctxpack.exe", "build/Release/cctxpack.exe",
                "build/cctxpack", "build-rel/cctxpack"):
        p = os.path.join(ROOT, rel)
        if os.path.isfile(p):
            return p
    sys.exit("cctxpack binary not found; pass --bin")


def run_count(binary, repo):
    """Per-file estimates, straight from the binary."""
    out = subprocess.run([binary, "count", repo, "--format", "json"],
                         capture_output=True)
    if out.returncode != 0:
        sys.exit("cctxpack count failed on %s: %s"
                 % (repo, out.stderr.decode("utf-8", "replace").strip()))
    return json.loads(out.stdout.decode("utf-8"))["files"]


def classify(data):
    """Dominant script class of a file, by codepoint."""
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return "non-utf8"
    cjk = cyr = latin = emoji = 0
    total = 0
    for ch in text:
        cp = ord(ch)
        total += 1
        if 0x4E00 <= cp <= 0x9FFF or 0x3040 <= cp <= 0x30FF:
            cjk += 1
        elif 0x0400 <= cp <= 0x04FF:
            cyr += 1
        elif 0x00C0 <= cp <= 0x024F:
            latin += 1
        elif 0x1F000 <= cp <= 0x1FAFF or 0x2600 <= cp <= 0x27BF:
            emoji += 1
    if total == 0:
        return "empty"
    for name, n in (("cjk", cjk), ("emoji", emoji), ("cyrillic", cyr),
                    ("latin", latin)):
        if n * 100 >= total:
            return name
    return "ascii"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("repos", nargs="+", help="repository paths")
    ap.add_argument("--bin", default=None)
    ap.add_argument("--encoding", default="cl100k_base")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    import tiktoken
    enc = tiktoken.get_encoding(args.encoding)
    binary = find_binary(args.bin)

    report = {"encoding": args.encoding, "corpora": []}
    for repo in args.repos:
        name = os.path.basename(repo.rstrip("/\\"))
        rows = run_count(binary, repo)
        per_file, skipped = [], 0
        for f in rows:
            if f.get("binary"):
                skipped += 1
                continue
            p = os.path.join(repo, f["path"].replace("/", os.sep))
            if not os.path.isfile(p):
                continue
            data = open(p, "rb").read()
            truth = len(enc._encode_bytes(data))
            if truth == 0:
                continue
            per_file.append((f["path"], f["tokens"], truth, classify(data),
                             len(data)))

        if not per_file:
            continue
        est = sum(r[1] for r in per_file)
        real = sum(r[2] for r in per_file)
        rels = sorted((r[1] - r[2]) / r[2] * 100 for r in per_file)
        n = len(rels)
        worst = min(per_file, key=lambda r: (r[1] - r[2]) / r[2])

        by_class = {}
        for r in per_file:
            by_class.setdefault(r[3], []).append((r[1] - r[2]) / r[2] * 100)
        classes = {}
        for k, v in sorted(by_class.items()):
            v.sort()
            classes[k] = {
                "n": len(v),
                "median": round(v[len(v) // 2], 2),
                "worst": round(v[0], 2),
            }

        corpus = {
            "name": name,
            "path": repo,
            "files_measured": n,
            "binary_skipped": skipped,
            "aggregate_pct": round((est - real) / real * 100, 2),
            "median_pct": round(rels[n // 2], 2),
            "p90_pct": round(rels[int(n * 0.9)], 2),
            "p10_pct": round(rels[int(n * 0.1)], 2),
            "worst_pct": round(rels[0], 2),
            "under_rate_pct": round(
                sum(1 for r in rels if r < 0) * 100.0 / n, 1),
            "summed_error_pct": round((est - real) / real * 100, 3),
            "est_total": est,
            "real_total": real,
            "worst_file": worst[0],
            "worst_file_pct": round((worst[1] - worst[2]) / worst[2] * 100, 2),
            "by_class": classes,
        }
        report["corpora"].append(corpus)

        if not args.quiet:
            print("\n%-24s %d files (%d binary skipped)"
                  % (name, n, skipped))
            print("  aggregate %+7.2f%%   median %+7.2f%%   p90 %+7.2f%%"
                  % (corpus["aggregate_pct"], corpus["median_pct"],
                     corpus["p90_pct"]))
            print("  worst     %+7.2f%%   (%s)   under %4.1f%%"
                  % (corpus["worst_pct"], worst[0],
                     corpus["under_rate_pct"]))
            print("  est %d vs real %d tokens"
                  % (est, real))
            for k, v in classes.items():
                print("    %-10s n=%-4d median %+7.2f%%  worst %+7.2f%%"
                      % (k, v["n"], v["median"], v["worst"]))

    if not report["corpora"]:
        sys.exit("no measurable files found")

    with open(os.path.join(HERE, "benchmark-results.json"), "w",
              encoding="utf-8") as fh:
        json.dump(report, fh, indent=2)
        fh.write("\n")

    md = ["# Token estimator benchmark",
          "",
          "Generated by `tools/benchmark.py` against `%s`." % args.encoding,
          "Estimates come from the shipped binary; binary files are excluded",
          "because cctxpack does not estimate them.",
          "",
          "| Corpus | Files | Aggregate | Median | p90 | Worst | Under |",
          "|---|---:|---:|---:|---:|---:|---:|"]
    for c in report["corpora"]:
        md.append("| %s | %d | %+.2f%% | %+.2f%% | %+.2f%% | %+.2f%% | %.1f%% |"
                  % (c["name"], c["files_measured"], c["aggregate_pct"],
                     c["median_pct"], c["p90_pct"], c["worst_pct"],
                     c["under_rate_pct"]))
    md += ["", "Per dominant script class:", "",
           "| Corpus | Class | Files | Median | Worst |", "|---|---|---:|---:|---:|"]
    for c in report["corpora"]:
        for k, v in c["by_class"].items():
            md.append("| %s | %s | %d | %+.2f%% | %+.2f%% |"
                      % (c["name"], k, v["n"], v["median"], v["worst"]))
    md.append("")
    with open(os.path.join(HERE, "benchmark-results.md"), "w",
              encoding="utf-8") as fh:
        fh.write("\n".join(md))

    print("\nwrote benchmark-results.json and benchmark-results.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())