#!/usr/bin/env python3
"""Calibrate cctxpack's token estimator against a reference BPE tokenizer.

Fits  T = p*C + q*L  over text files, where
    C = UTF-8 code points (bytes that are not 0x80..0xBF)
    L = number of newlines
    T = tokens from the reference encoding
and reports the equivalent two-parameter formula used by src/tokens.c,

    est = ceil((C + alpha*L) / beta)        alpha = q/p,  beta = 1/p

Fitting methods:
    ols   ordinary least squares, no intercept (minimises absolute error)
    wls   weighted least squares with w = 1/T^2 (minimises relative error)
    safe  grid search for the smallest (over-counting) estimate that never
          under-counts a file >= --min-tokens

Calibration corpus: the repository's *first-party* text files (source, tests,
docs, tools).  Vendored third-party code is excluded with --exclude-prefix
vendor/ because its token density is unrepresentative and it dominates the
token mass.  See docs/design-document.md §4.6 for the recorded result.

Usage:
    python tools/calibrate_tokens.py [--encoding NAME] [--all-encodings]
        [--fit ols|wls|safe] [--target-over PCT] [--exclude-prefix P]...
        [--show-worst] [--sweep] [paths...]

With no paths the corpus is `git ls-files`' text files from the repo root.
Requires the `tiktoken` package (pip install tiktoken).
"""

import argparse
import math
import subprocess
import sys

try:
    import tiktoken
except ImportError:  # pragma: no cover
    sys.exit("tiktoken is not installed (pip install tiktoken)")


def code_points(data: bytes) -> int:
    return sum(1 for b in data if not 0x80 <= b <= 0xBF)


def collect_from_git() -> list[str]:
    out = subprocess.run(
        ["git", "ls-files", "-c", "-o", "--exclude-standard"],
        capture_output=True, text=True, check=True).stdout
    return [p for p in out.splitlines() if p]


def load_corpus(paths: list[str], excludes: list[str]):
    corpus = []
    for p in paths:
        if any(p.startswith(e) for e in excludes):
            continue
        try:
            with open(p, "rb") as fh:
                data = fh.read()
        except OSError:
            continue
        if b"\x00" in data:
            continue
        try:
            data.decode("utf-8")
        except UnicodeDecodeError:
            continue
        corpus.append((p, data))
    return corpus


def points_of(corpus, enc):
    return [(code_points(d), d.count(b"\n"),
             len(enc.encode(d.decode("utf-8")))) for _, d in corpus]


def wfit(points, wfun):
    sc2 = scl = sl2 = sct = slt = 0.0
    for c, l, t in points:
        w = wfun(t) if t else 0.0
        sc2 += w * c * c
        scl += w * c * l
        sl2 += w * l * l
        sct += w * c * t
        slt += w * l * t
    det = sc2 * sl2 - scl * scl
    if det == 0:
        raise SystemExit("degenerate corpus")
    # solve  [sc2 scl][p]   [sct]
    #        [scl sl2][q] = [slt]
    p = (sl2 * sct - scl * slt) / det
    q = (sc2 * slt - scl * sct) / det
    if p <= 0:
        raise SystemExit("degenerate fit (p <= 0)")
    return q / p, 1.0 / p


def fit_safe(points, min_tokens):
    """Grid-search (alpha, beta) that never under-counts a file >= min_tokens."""
    big = [pt for pt in points if pt[2] >= min_tokens]
    best = None
    alpha = 0.0
    while alpha <= 40.0:
        beta = 2.0
        while beta <= 7.0:
            worst = max(((t - math.ceil((c + alpha * l) / beta)) / t
                         for c, l, t in big), default=0.0)
            if worst <= 0.0:
                mae = sum(abs(math.ceil((c + alpha * l) / beta) - t)
                          for c, l, t in points) / len(points)
                if best is None or (mae, beta) < best[0]:
                    best = ((mae, beta), alpha, beta)
            beta += 0.02
        alpha += 0.25
    if best is None:
        raise SystemExit("no (alpha, beta) satisfies the safety constraint")
    return best[1], best[2]


def metrics(points, alpha, beta, min_tokens):
    est = [(c, l, t, math.ceil((c + alpha * l) / beta)) for c, l, t in points]
    n = len(est)
    tot_est = sum(e for _, _, _, e in est)
    tot_tok = sum(t for _, _, t, _ in est)
    rel = sorted(abs(e - t) / t for _, _, t, e in est if t)
    wub = max(((t - e) / t for _, _, t, e in est if t >= min_tokens),
              default=0.0)
    return {
        "n": n,
        "mae": sum(abs(e - t) for _, _, t, e in est) / n if n else 0.0,
        "median": 100.0 * rel[n // 2] if rel else 0.0,
        "p90": 100.0 * rel[int(0.9 * (n - 1))] if rel else 0.0,
        "agg": 100.0 * (tot_est - tot_tok) / tot_tok if tot_tok else 0.0,
        "under": sum(1 for _, _, t, e in est if e < t),
        "worst_under": 100.0 * wub,
        "tot_est": tot_est,
        "tot_tok": tot_tok,
    }


def report(corpus, enc, method, target_over, min_tokens, show_worst,
           force=None):
    points = points_of(corpus, enc)
    if force:
        alpha, beta = force
    elif method in ("ols", "wls"):
        wfun = (lambda t: 1.0) if method == "ols" else (lambda t: 1.0 / (t * t))
        alpha, beta = wfit(points, wfun)
    else:
        alpha, beta = fit_safe(points, min_tokens)
    if target_over:
        m = metrics(points, alpha, beta, min_tokens)
        beta *= m["tot_est"] / ((1.0 + target_over / 100.0) * m["tot_tok"])
    m = metrics(points, alpha, beta, min_tokens)
    print(f"  fit={method} target-over={target_over:g}% "
          f"alpha={alpha:.3f} beta={beta:.4f}")
    print(f"  files {m['n']}  MAE {m['mae']:.1f}  median {m['median']:.1f}%  "
          f"p90 {m['p90']:.1f}%")
    print(f"  under-estimated {m['under']}/{m['n']}  "
          f"worst-under(>= {min_tokens} tok) {m['worst_under']:.2f}%")
    print(f"  aggregate {m['tot_est']} est vs {m['tot_tok']} actual "
          f"({m['agg']:+.2f}%)")
    if not show_worst:
        return
    rows = [(abs(math.ceil((c + alpha * l) / beta) - t) / t if t else 0.0,
             path, c, l, t, math.ceil((c + alpha * l) / beta))
            for (c, l, t), (path, _) in zip(points, corpus)]
    print("    worst over-estimates (est - T):")
    for relerr, path, c, l, t, est in sorted(rows, reverse=True)[:6]:
        print(f"      {100*relerr:6.1f}%  {path}  C={c} L={l} T={t} est={est}")
    print("    worst under-estimates (T - est):")
    for relerr, path, c, l, t, est in sorted(
            rows, key=lambda r: (r[4] - r[5]) / r[4] if r[4] else 0,
            reverse=True)[:6]:
        print(f"      {100*relerr:6.1f}%  {path}  C={c} L={l} T={t} est={est} "
              f"(T-est={t-est})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--encoding", default="cl100k_base")
    ap.add_argument("--all-encodings", action="store_true")
    ap.add_argument("--fit", choices=("ols", "wls", "safe"), default="ols")
    ap.add_argument("--target-over", type=float, default=0.0,
                    help="percent by which the corpus aggregate is over-counted")
    ap.add_argument("--min-tokens", type=int, default=100,
                    help="ignore files below this token count in safety metrics")
    ap.add_argument("--alpha", type=float, help="force alpha (with --beta)")
    ap.add_argument("--beta", type=float, help="force beta (with --alpha)")
    ap.add_argument("--exclude-prefix", action="append", default=[],
                    help="skip corpus paths starting with this prefix")
    ap.add_argument("--show-worst", action="store_true")
    ap.add_argument("--sweep", action="store_true")
    ap.add_argument("paths", nargs="*")
    args = ap.parse_args()

    paths = args.paths or collect_from_git()
    corpus = load_corpus(paths, args.exclude_prefix)
    if not corpus:
        raise SystemExit("empty corpus")
    total_bytes = sum(len(d) for _, d in corpus)
    excl = f" (excluding {', '.join(args.exclude_prefix)})" if args.exclude_prefix else ""
    print(f"corpus: {len(corpus)} text files, {total_bytes} bytes{excl}")

    names = (["cl100k_base", "o200k_base"] if args.all_encodings
             else [args.encoding])
    for name in names:
        print(f"\n{name}:")
        enc = tiktoken.get_encoding(name)
        if args.sweep:
            points = points_of(corpus, enc)
            if args.fit in ("ols", "wls"):
                wfun = ((lambda t: 1.0) if args.fit == "ols"
                        else (lambda t: 1.0 / (t * t)))
                a0, b0 = wfit(points, wfun)
            else:
                a0, b0 = fit_safe(points, args.min_tokens)
            print(f"  {'target-over':>11} {'alpha':>8} {'beta':>8} "
                  f"{'worst-under':>11} {'MAE':>9}")
            for over in (0, 2, 5, 10, 15, 20, 25):
                a, b = a0, b0
                if over:
                    m0 = metrics(points, a, b, args.min_tokens)
                    b *= m0["tot_est"] / ((1.0 + over / 100.0) * m0["tot_tok"])
                m = metrics(points, a, b, args.min_tokens)
                print(f"  {over:>10}% {a:>8.3f} {b:>8.3f} "
                      f"{m['worst_under']:>10.2f}% {m['mae']:>9.1f}")
        else:
            force = ((args.alpha, args.beta)
                     if args.alpha and args.beta else None)
            report(corpus, enc, args.fit, args.target_over, args.min_tokens,
                   args.show_worst, force)


if __name__ == "__main__":
    main()
