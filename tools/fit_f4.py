#!/usr/bin/env python3
"""Fit the F4 feature model for src/tokens.c.

Feature definitions here are NORMATIVE: they are written to match the C
implementation exactly (see the header comment in src/tokens.c).  If the two
ever disagree, the coefficients are wrong.

    F1   8   character-class counts    (same classifier as the shipped model)
    F2  10   line-length histogram     bin = min(codepoints_on_line / 20, 9)
    F3  12   word-length histogram     word = maximal run of word characters,
                                       bin = min(len - 1, 11)
    F4   7   whitespace-run histogram  run = maximal run of whitespace
                                       (newline counts; runs span lines)
    bias 1   constant
                                        -> 38 features

The fit is a NON-NEGATIVE least squares with a ridge pull toward the currently
shipped model:

    min_{w >= 0}  ||Xw - y||^2 + lam * ||w - w_shipped||^2

Regularisation is not optional here.  The features are massively collinear
(every histogram is near-proportional to file size) and the two training corpora
have very different script mixes, so unconstrained NNLS answers with nonsense -
it assigned latin = 56 tokens per code point and line6 = +104 tokens per line
when fitted on the pure-ASCII corpus, and it exploded on integer rounding.
Tying the solution to the shipped model means an unsupported feature simply
keeps today's behaviour, and a histogram correction is only paid for if the data
earns it.

lambda is chosen by leave-one-repo-out cross-validation.

Usage:
    python tools/fit_f4.py
"""
import json
import math
import os
import subprocess
import sys

REPOS = {"sniff": "C:/Old projects/sniff",
         "cctxpack": "C:/New projects/C projects/cctxpack"}

CLS = ["ascii", "nl", "latin", "cyrillic", "cjk", "emoji", "marks", "other"]
NCLS = 8
NLINE = 10
NWORD = 12
NWS = 7
NFEAT = NCLS + NLINE + NWORD + NWS + 1

LINE_BINS = 20
WORD_CLASSES = ("ascii", "latin", "cyrillic", "cjk")
SHIPPED = [264, 1185, 1389, 377, 880, 2495, 982, 884]
PRIOR = [v / 1000.0 for v in SHIPPED] + [0.0] * (NFEAT - NCLS)

FEAT_NAMES = (CLS
              + ["line%d" % i for i in range(NLINE)]
              + ["word%d" % i for i in range(NWORD)]
              + ["ws%d" % i for i in range(NWS)]
              + ["bias"])


def inr(cp, lo, hi):
    return lo <= cp <= hi


def classify(cp):
    """Port of classify() in src/tokens.c.  Keep the two in sync."""
    if cp == 0x0A:
        return 1
    if cp < 0x80:
        return 0
    if (inr(cp, 0x00C0, 0x024F) or inr(cp, 0x1E00, 0x1EFF)
            or inr(cp, 0x2C60, 0x2C7F) or inr(cp, 0xA720, 0xA7FF)):
        return 2
    if inr(cp, 0x0400, 0x052F) or inr(cp, 0x2DE0, 0x2DFF) or inr(cp, 0xA640, 0xA69F):
        return 3
    if (inr(cp, 0x0900, 0x097F) or inr(cp, 0x0980, 0x09FF) or inr(cp, 0x0A80, 0x0AFF)
            or inr(cp, 0x0B00, 0x0B7F) or inr(cp, 0x0B80, 0x0BFF)
            or inr(cp, 0x0C00, 0x0C7F) or inr(cp, 0x0C80, 0x0CFF)
            or inr(cp, 0x0D00, 0x0D7F) or inr(cp, 0x0D80, 0x0DFF)
            or inr(cp, 0x0E00, 0x0E7F) or inr(cp, 0x0E80, 0x0EFF)):
        return 6
    if (inr(cp, 0x1100, 0x11FF) or inr(cp, 0x3040, 0x30FF) or inr(cp, 0x3130, 0x318F)
            or inr(cp, 0x31F0, 0x31FF) or inr(cp, 0x3400, 0x4DBF) or inr(cp, 0x4E00, 0x9FFF)
            or inr(cp, 0xA960, 0xA97F) or inr(cp, 0xAC00, 0xD7AF) or inr(cp, 0xF900, 0xFAFF)
            or inr(cp, 0xFF66, 0xFF9F) or inr(cp, 0x20000, 0x2A6DF)):
        return 4
    if (cp in (0x203C, 0x2049, 0x2122, 0x2139, 0x24C2, 0x25AA, 0x2328, 0x231A, 0x231B,
               0x23CF, 0x3030, 0x303D, 0x3297, 0x3299, 0xFE0F)
            or inr(cp, 0x2194, 0x21AA) or inr(cp, 0x23E9, 0x23FA) or inr(cp, 0x25AA, 0x25FE)
            or inr(cp, 0x2600, 0x27BF) or inr(cp, 0x2934, 0x2935) or inr(cp, 0x2B00, 0x2BFF)
            or inr(cp, 0x1F000, 0x1FAFF)):
        return 5
    return 7


def is_word(cp, k):
    if k == 0:
        return (0x30 <= cp <= 0x39) or (0x41 <= cp <= 0x5A) or (0x61 <= cp <= 0x7A)
    return CLS[k] in WORD_CLASSES


def is_ws(cp, k):
    if k == 1:
        return True
    if k == 0:
        return cp in (0x20, 0x09, 0x0B, 0x0C, 0x0D)
    return cp == 0xA0


def ws_bin(n):
    return n - 1 if n <= 5 else (5 if n <= 9 else 6)


def features(text):
    f = [0] * NFEAT
    line = word = ws = 0
    for ch in text:
        cp = ord(ch)
        k = classify(cp)
        f[k] += 1
        if k == 1:
            f[NCLS + min(line // LINE_BINS, NLINE - 1)] += 1
            line = 0
            if word:
                f[NCLS + NLINE + min(word - 1, NWORD - 1)] += 1
                word = 0
            ws += 1
        elif is_word(cp, k):
            if ws:
                f[NCLS + NLINE + NWORD + ws_bin(ws)] += 1
                ws = 0
            word += 1
            line += 1
        else:
            if word:
                f[NCLS + NLINE + min(word - 1, NWORD - 1)] += 1
                word = 0
            if is_ws(cp, k):
                ws += 1
            line += 1
    if word:
        f[NCLS + NLINE + min(word - 1, NWORD - 1)] += 1
    if ws:
        f[NCLS + NLINE + NWORD + ws_bin(ws)] += 1
    tail = len(text) - text.rfind("\n") - 1
    if tail > 0 or (text and text[-1] != "\n"):
        f[NCLS + min(min(tail, 199) // LINE_BINS, NLINE - 1)] += 1
    f[-1] = 1
    return f


def col_scale(X):
    """L1 column norm.

    This matters more than the solver choice.  Fitted on raw counts, the `ascii`
    column carries values ~1e4 and `line6` ~1e2, so any coordinate-wise solver
    takes catastrophically unbalanced steps and lands on a degenerate point of
    the collinear solution set (it put latin at 56 tokens per code point and
    line6 at +104 per line, which then explodes on integer rounding).
    Normalising every column to unit L1 norm makes the optimisation well scaled;
    coefficients are mapped back to real units afterwards.
    """
    s = [sum(X[k][j] for k in range(len(X))) for j in range(len(X[0]))]
    return [v if v else 1.0 for v in s]


def ridge_nnls(X, y, prior, lam, iters=250):
    """Non-negative coordinate descent with a ridge pull toward `prior`.

        w_j = (z_j.y - sum_{i!=j} w_i (z_j.z_i) + lam*prior_j) / (z_j.z_j + lam)

    where Z = X / colscale and prior is expressed in the same normalised units.
    """
    n = len(prior)
    nrows = len(X)
    w = list(prior)
    zz = [[X[k][j] ** 2 for k in range(nrows)] for j in range(n)]
    zy = [sum(X[k][j] * y[k] for k in range(nrows)) for j in range(n)]
    zcols = [[X[k][j] for k in range(nrows)] for j in range(n)]
    for _ in range(iters):
        for j in range(n):
            denom = sum(zz[j]) + lam
            if denom <= 0.0:
                continue
            num = zy[j] + lam * prior[j]
            zj = zcols[j]
            for k in range(nrows):
                if zj[k] == 0.0:
                    continue
                s = 0.0
                for i in range(n):
                    if i != j:
                        s += w[i] * X[k][i]
                num -= zj[k] * s
            v = num / denom
            w[j] = v if v > 0.0 else 0.0
    return w


def fit(X, y, lam):
    """Fit in normalised space, return coefficients in REAL units."""
    s = col_scale(X)
    Z = [[X[k][j] / s[j] for j in range(len(X[0]))] for k in range(len(X))]
    prior_n = [PRIOR[j] * s[j] for j in range(len(PRIOR))]
    v = ridge_nnls(Z, y, prior_n, lam)
    return [v[j] / s[j] for j in range(len(v))]


def is_binary(data):
    """Same sniff cctxpack uses: a NUL byte in the first 8192 bytes.

    Binary files are NOT tokenised by cctxpack - it emits a placeholder line
    instead.  Leaving them in the fit was polluting the regression badly: a
    __pycache__/*.pyc measured -61% and a blob.bin -54%, both of which the tool
    never actually estimates.
    """
    return b"\0" in data[:8192]


def load(repo):
    import tiktoken
    enc = tiktoken.get_encoding("cl100k_base")
    root = REPOS[repo]
    files = subprocess.run(["git", "-C", root, "ls-files", "-c", "-o", "--exclude-standard"],
                           capture_output=True, text=True, encoding="utf-8").stdout.split()
    rows = []
    skipped = 0
    for f in files:
        if "__pycache__" in f or f.endswith(".pyc"):
            skipped += 1
            continue
        p = os.path.join(root, f.replace("/", os.sep))
        if not os.path.isfile(p):
            continue
        data = open(p, "rb").read()
        if not data or is_binary(data):
            skipped += 1
            continue
        rows.append((features(data.decode("utf-8", errors="replace")),
                     len(enc._encode_bytes(data)), f))
    print("   (%d binary/cache files skipped)" % skipped)
    return rows


def predict(rows, w):
    return [sum(r[j] * w[j] for j in range(len(w))) for r, _, _ in rows]


def stats(rows, pred):
    rels, te, tc = [], 0.0, 0.0
    for i, (_, t, _) in enumerate(rows):
        te += pred[i]
        tc += t
        if t:
            rels.append((pred[i] - t) / t * 100)
    if not rels:
        return 0.0, 0.0, 0.0, 0.0, 0.0
    rels.sort()
    return ((te - tc) / tc * 100, rels[len(rels) // 2], rels[int(len(rels) * 0.9)],
            rels[0], sum(1 for r in rels if r < 0) * 100.0 / len(rels))


def line(name, rows, w):
    a, m, p9, wo, u = stats(rows, predict(rows, w))
    print("  %-9s agg %+7.2f%%  med %+7.2f%%  p90 %+7.2f%%  worst %+7.2f%%  under %5.1f%%"
          % (name, a, m, p9, wo, u))
    return abs(m)


def main():
    data = {r: load(r) for r in REPOS}
    for r, rows in data.items():
        print('%s: %d files' % (r, len(rows)))

    # lambda by leave-one-repo-out CV.  Objective is SAFETY-CONSTRAINED: under-
    # estimating overflows the consumer context window, so reject any lambda whose
    # held-out under-rate is worse than the shipped model, then take the lowest
    # |median error| among survivors.  Minimising |median error| outright picks an
    # unsafe point (74.6% of files under-estimated).
    print('')
    print('leave-one-repo-out CV over lambda:')
    gate = 32.0
    best = None
    for lam in (1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6):
        tot = 0.0; worst_under = 0.0
        for tr, ev in ((data['sniff'], data['cctxpack']), (data['cctxpack'], data['sniff'])):
            w = fit([f for f, _, _ in tr], [float(t) for _, t, _ in tr], lam)
            _, m, _, _, u = stats(ev, predict(ev, w))
            tot += abs(m); worst_under = max(worst_under, u)
        ok = worst_under <= gate
        print('  lam=%-8.0e sum|median|=%7.2f  worst under%%=%5.1f  %s'
              % (lam, tot, worst_under, 'PASS' if ok else 'reject'))
        if ok and (best is None or tot < best[0]):
            best = (tot, lam)
    lam = best[1]
    print('')
    print('chosen lambda = %.0e' % lam)

    pooled = data['sniff'] + data['cctxpack']
    w = fit([f for f, _, _ in pooled], [float(t) for _, t, _ in pooled], lam)

    print('')
    print('shipped model (reference):')
    line('sniff', data['sniff'], PRIOR)
    line('cctxpack', data['cctxpack'], PRIOR)
    print('refit F1+F4 pooled, IN-CORPUS:')
    line('sniff', data['sniff'], w)
    line('cctxpack', data['cctxpack'], w)
    print('leave-one-repo-out (the honest number):')
    for tr, ev, nm in ((data['sniff'], data['cctxpack'], 'sniff->cctxpack'),
                       (data['cctxpack'], data['sniff'], 'cctxpack->sniff')):
        line(nm, ev, fit([f for f, _, _ in tr], [float(t) for _, t, _ in tr], lam))

    ir = [int(math.ceil(v * 1000.0)) for v in w]
    wi = [v / 1000.0 for v in ir]
    print('')
    print('integer thousandths (ceil), pooled fit IN-CORPUS:')
    line('sniff', data['sniff'], wi)
    line('cctxpack', data['cctxpack'], wi)
    print('')
    print('coefficients (integer thousandths; class rates, then corrections):')
    for n, v in zip(FEAT_NAMES, ir):
        tag = '   (shipped %d)' % SHIPPED[CLS.index(n)] if n in CLS else ''
        print('    %-8s %6d%s' % (n, v, tag))
    print('')
    print('JSON: ' + json.dumps(ir))
    return 0


if __name__ == '__main__':
    sys.exit(main())
