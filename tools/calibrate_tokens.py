#!/usr/bin/env python3
"""Re-derive and re-measure the script-aware token estimator (design doc 4.6).

Rates in src/tokens.c are per-class tokens-per-code-point in thousandths.  They
were produced by this script: tokenize single-class samples with both
`cl100k_base` and `o200k_base`, subtract the ASCII and newline contribution, take
the median residual per class, multiply by 1.25, and round up.

The 1.25 bias is deliberate.  Over-estimating wastes a little of the user's
budget; under-estimating overflows the model's context window.

Two rules learned the hard way, both encoded here:
  * coefficients are DERIVED from the synthetic single-class samples only, and
    VALIDATED on the repository's own files.  Fitting on the repo files once
    produced a rate of 203,010 tokens/char, because a stray arrow character with
    a count of 1 divided into the residual.
  * an aggregate accuracy figure is not an acceptance metric.  The previous
    model reported +2.07% aggregate while 69 of 124 observations were
    under-estimates and the worst was -85.6%.  This script always prints the
    per-class distribution and the under-estimate tail.

Usage:
    python tools/calibrate_tokens.py            # derive + validate
    python tools/calibrate_tokens.py --compare # also print the old model's error
"""
import os
import statistics
import sys

try:
    import tiktoken
except ImportError:
    sys.exit("tiktoken is required: pip install tiktoken")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Must mirror classify() in src/tokens.c exactly.
MARKS = [(0x0900, 0x097F), (0x0980, 0x09FF), (0x0A80, 0x0AFF), (0x0B00, 0x0B7F),
         (0x0B80, 0x0BFF), (0x0C00, 0x0C7F), (0x0C80, 0x0CFF), (0x0D00, 0x0D7F),
         (0x0D80, 0x0DFF), (0x0E00, 0x0E7F), (0x0E80, 0x0EFF)]
RANGES = [
    ("cjk", [(0x3400, 0x4DBF), (0x4E00, 0x9FFF), (0xF900, 0xFAFF), (0x3040, 0x30FF),
             (0x31F0, 0x31FF), (0xFF66, 0xFF9F), (0x1100, 0x11FF), (0x3130, 0x318F),
             (0xAC00, 0xD7AF), (0x20000, 0x2A6DF)]),
    ("emoji", [(0x203C, 0x203C), (0x2049, 0x2049), (0x2122, 0x2122), (0x2139, 0x2139),
               (0x2194, 0x21AA), (0x231A, 0x231B), (0x2328, 0x2328), (0x23CF, 0x23CF),
               (0x23E9, 0x23FA), (0x24C2, 0x24C2), (0x25AA, 0x25FE), (0x2600, 0x27BF),
               (0x2934, 0x2935), (0x2B00, 0x2BFF), (0x3030, 0x3030), (0x303D, 0x303D),
               (0x3297, 0x3297), (0x3299, 0x3299), (0x1F000, 0x1FAFF), (0xFE0F, 0xFE0F),
               (0x1F1E6, 0x1F1FF)]),
    ("cyrillic", [(0x0400, 0x052F), (0x2DE0, 0x2DFF), (0xA640, 0xA69F)]),
    ("latin_ext", [(0x00C0, 0x024F), (0x1E00, 0x1EFF), (0x2C60, 0x2C7F),
                   (0xA720, 0xA7FF)]),
]
CLASSES = ["ascii", "nl", "latin_ext", "cyrillic", "cjk", "emoji", "marks", "other"]
IDX = {c: i for i, c in enumerate(CLASSES)}
NONASCII = [i for i, c in enumerate(CLASSES) if c not in ("ascii", "nl")]

# The rates currently compiled into src/tokens.c.
SHIPPED = {"ascii": 264, "nl": 1185, "latin_ext": 1389, "cyrillic": 377,
           "cjk": 880, "emoji": 2495, "marks": 982, "other": 884}
BIAS = 1.25

# Derivation corpus: each string contains exactly ONE non-ASCII class.
SYNTH = {
    'latin_ext': [
        "Le café est naïve, le résumé est prêt. Voilà, maïtre, crème brûlée.\n",
        "El niño comía señas y pan. Año nuevo, coraſal, vehículo, cámara.\n",
        "São Paulo, México, Zürich, Køln, łódź,ąćęśż.\n",
    ],
    'cyrillic': [
        "Это предложение для проверки токенов в слове.\n",
        "Вв должны проверить эту текст.\n",
        "Привет, мир! Всемир,добрый день, ночь.\n",
    ],
    'cjk': [
        "这是一个用于测试的中文文本。包含一些技术术语。\n",
        "在计算机科学中，算法的复杂度是关键的。\n",
        "これは日本語の文です。アルゴリズムの評価をします。\n",
        "안녕다하세요 이것은 한글어 문장입니다.\n",
    ],
    'emoji': [
        "🚀🚀🚀🚀🚀🚀\n",
        "🚀🚀🚀 ✅✅ 🎉🎉 done📦📦\n",
        "⚠️⚠️ warning, 🐛🐛 bug found 💡💡 fix💡💡\n",
        "👨‍👩‍👧‍👦 🏳️‍🌈 👨‍👩‍👧‍👦\n",
    ],
    'marks': [
        "हिन्दी जाने की भाषा जाने सैɌ\n",
        "ไทย ภาษา ไทย สวัดีมาษาความ\n",
    ],
    'other': [
        "ΑβγδΕ ζηθικλμ μνξυσφος\n",
        "שלום עולם דברך אישראל בשביל\n",
        "العربية مرحبا بكم العالم والأسراء\n",
    ],
}
MIXED = [
    "func main() { \u8fd9\u91cc\u662f\u4e2d\u6587 /* \u043a\u043e\u043c\u043c"
    "\u0435\u043d\u0442 */ \U0001F680 return 42; }\n",
    "# \u30c6\u30b9\u30c8\nint x = 1; // caf\u00e9 \u2713\n",
]


def classify(ch):
    cp = ord(ch)
    if cp == 0x0A:
        return "nl"
    if cp < 0x80:
        return "ascii"
    for lo, hi in MARKS:
        if lo <= cp <= hi:
            return "marks"
    for name, rs in RANGES:
        for lo, hi in rs:
            if lo <= cp <= hi:
                return name
    return "other"


def feats(text):
    f = [0] * len(CLASSES)
    for ch in text:
        f[IDX[classify(ch)]] += 1
    return f


def repo_files():
    out = []
    for dp, dn, fn in os.walk(ROOT):
        if any(x in dp for x in ("\\build", "\\.git")):
            continue
        for f in fn:
            if os.path.splitext(f)[1].lower() in (".c", ".h", ".md", ".py",
                                                    ".txt", ".json", ".yml"):
                try:
                    t = open(os.path.join(dp, f), encoding="utf-8").read()
                except Exception:
                    continue
                if t:
                    out.append((os.path.relpath(os.path.join(dp, f), ROOT), t))
    return out


def derive(cl, o2):
    rates = {}
    for c in CLASSES:
        if c in ("ascii", "nl"):
            continue
        i = IDX[c]
        rs = []
        for texts in SYNTH.values():
            for t in texts:
                f = feats(t)
                if [j for j in NONASCII if f[j] > 0] != [i] or f[i] < 5:
                    continue
                base = (SHIPPED["ascii"] * f[IDX["ascii"]]
                        + SHIPPED["nl"] * f[IDX["nl"]]) / 1000.0
                for truth in (len(cl.encode(t)), len(o2.encode(t))):
                    rs.append((truth - base) / f[i])
        if not rs:
            raise SystemExit("no derivation samples for class %r" % c)
        rates[c] = -(-int(statistics.median(rs) * BIAS * 1000) // 1)
    return rates


def evaluate(rates, cl, o2, texts):
    rels, by = [], {}
    for item in texts:
        t = item[1] if isinstance(item, tuple) else item
        f = feats(t)
        p = sum(a * rates[c] for a, c in zip(f, CLASSES)) / 1000.0
        present = [CLASSES[j] for j in NONASCII if f[j] > 0]
        dom = present[0] if len(present) == 1 else ("ascii" if not present else "mixed")
        for truth in (len(cl.encode(t)), len(o2.encode(t))):
            r = (p - truth) / truth * 100 if truth else 0.0
            rels.append(r)
            by.setdefault(dom, []).append(r)
    rels.sort()
    return rels, by


def report(label, rels, by):
    print("%s" % label)
    print("  median %+.1f%%   p90 %+.1f%%   worst %+.1f%%   "
          "under-estimates %d/%d"
          % (rels[len(rels) // 2], rels[int(len(rels) * 0.9)], rels[0],
             sum(1 for r in rels if r < 0), len(rels)))
    print("  per-class median: "
          + "  ".join("%s %+.0f%%" % (k, sorted(v)[len(v) // 2])
                      for k, v in sorted(by.items()) if k != "mixed"))


def main():
    compare = "--compare" in sys.argv
    cl = tiktoken.get_encoding("cl100k_base")
    o2 = tiktoken.get_encoding("o200k_base")

    print("reference: tiktoken cl100k_base + o200k_base\n")
    print("DERIVATION (synthetic single-class samples, bias x%.2f)" % BIAS)
    derived = derive(cl, o2)
    for c in CLASSES:
        rate = SHIPPED[c] if c in ("ascii", "nl") else derived[c]
        mark = "" if rate == SHIPPED[c] else "   <- differs from src/tokens.c"
        print("  %-11s shipped %5d   re-derived %5d%s"
              % (c, SHIPPED[c], rate, mark))

    texts = repo_files()
    names = [n for n, _ in texts]
    bodies = [t for _, t in texts]
    valid = bodies + [t for v in SYNTH.values() for t in v] + MIXED

    print("\nVALIDATION (repo files + synth + mixed; not used for derivation)")
    rels, by = evaluate(SHIPPED, cl, o2, valid)
    report("shipped rates:", rels, by)
    rels2, by2 = evaluate(dict(SHIPPED, **derived), cl, o2, valid)
    report("re-derived rates:", rels2, by2)

    print("\nEND TO END on this repo (shipped rates, per file)")
    rels3, _ = evaluate(SHIPPED, cl, o2, bodies)
    tot_est = tot_c = 0
    worst = (0.0, "")
    for (name, t), f in zip(texts, [feats(t) for _, t in texts]):
        p = sum(a * SHIPPED[c] for a, c in zip(f, CLASSES)) / 1000.0
        a = len(cl.encode(t))
        tot_est += p
        tot_c += a
        if a and (p - a) / a * 100 < worst[0]:
            worst = ((p - a) / a * 100, name)
    print("  %d files   aggregate %+.2f%% vs cl100k   worst %+.1f%% (%s)"
          % (len(texts), (tot_est - tot_c) / tot_c * 100, worst[0], worst[1]))
    if compare:
        print("\nCOMPARISON with the pre-2026-17 model (every class at the ASCII rate)")
        old = dict(SHIPPED)
        for c in CLASSES:
            if c not in ("ascii", "nl"):
                old[c] = SHIPPED["ascii"]
        rels4, by4 = evaluate(old, cl, o2, valid)
        report("old model:", rels4, by4)
    return 0


if __name__ == "__main__":
    sys.exit(main())