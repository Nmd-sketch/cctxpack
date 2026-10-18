# Design Document — cctxpack

| | |
|---|---|
| **Version** | 0.1.0-draft |
| **Date** | 2026-10-16 |
| **Status** | Draft — for architecture review |
| **Owner** | Nmd-sketch |
| **Companion** | `docs/technical-requirements-specification.md` (WHAT) — this document defines HOW |

## 1. Goals & design principles

- **P1 — Least code.** We build only what vendored libraries cannot do. Every module
  has a hard LOC budget (§2.2); blowing it triggers a design review, not more code.
- **P2 — Tests for correctness.** No module merges without unit tests; every command
  has a golden end-to-end test. Invariants are checked in C, not by eyeballing.
- **P3 — No unapproved cleverness.** Clever/indirect solutions require explicit
  approval, recorded in §3 (approved/rejected with rationale).
- **P4 — Determinism.** Byte-identical output for identical inputs; sorted
  everywhere; no locale, address, or hash-map iteration order in output.
- **P5 — Vendor > build.** Proven single-module libraries, committed, offline builds.
- **P6 — Pipe cleanliness.** stdout = data, stderr = diagnostics/report. The tool is
  composable in shell pipelines by construction.

## 2. Architecture

### 2.1 Overview
Single-threaded batch pipeline, one process, no daemon:

```
argv ─▶ cli ──▶ config merge ──▶ discover ──▶ filter ──▶ read ──▶ fit/budget ──▶ format ──▶ emit
              flags > file     git|walk    globs   sniff+trunc  estimate    md/xml/json/txt  stdout/-o/clip
                                                                        └──▶ report (stderr)
```

Each stage is a pure function over a growing context struct where practical
(discover/filter/read take injected "run git" and "open file" hooks in tests).

### 2.2 Modules & LOC budgets (own code; vendor/tests excluded)

LOC = physical lines (`wc -l`), blanks and comments included. The cap is the
review threshold (P1). The **M2** column records the measurement at the M2 code
review (2026-10-16); the **M3** column is the measurement at the M3 review
(2026-10-16, post `8317a60`). Caps were set to measured value + ~15% headroom.

| Module | Responsibility | LOC cap | LOC (M2) | LOC (M3) | Unit tests |
|---|---|---|---|---|---|
| `src/main.c` | command dispatch, version, `config_apply`, commit resolution | 100 | 30 | 43 | e2e smoke |
| `src/cli.c` | getopt wrapper, command-aware parsing, flag validation, path-operand resolution, `--help` text | 520 | 373 | 455 | `test_cli.c` |
| `src/cmd.c` | `count` / `tree` / `diff` command bodies, truncation reporting | 300 | - | 257 | `test_cli.c` |
| `src/config.c` | `.cctxpack.json` discovery + load (cJSON), precedence merge | 340 | – | 269 | `test_config.c` |
| `src/files.c` | `git ls-files` runner, fallback walker, git top/status/patch/commit runners, strlist, sort modes, .cctxpackignore | 650 | 320 | 583 | `test_files.c` |
| `src/glob.c` | glob matcher + include/exclude/root filters | 200 | 157 | 148 | `test_files.c` |
| `src/read.c` | open/read, binary sniff, read + token caps, truncation reasons (M11) | 215 | 149 | 186 | `test_read.c` |
| `src/tokens.c` | script-aware calibrated estimator | 180 | 30 | 128 | `test_tokens.c` |
| `src/score.c` | lexical path scoring for `--query` (extracted from `pack.c`, M8) | 80 | - | 58 | `test_pack.c` |
| `src/fit.c` | budget fitting, extracted from `pack.c` (M10) | 60 | - | 22 | `test_pack.c` |
| `src/outline.c` | declaration-outline scanner + refusal fallback (M10) | 620 | - | 534 | `test_outline.c` |
| `src/redact.c` | opt-in credential redaction (M9): vendor shapes, assignments, PEM blocks | 300 | - | 266 | `test_redact.c` |
| `src/pack.c` | ordering, sink, budget fit, report assembly, `--patch` bundle, manifest, redaction + outline wiring | 650 | 239 | 598 | `test_pack.c` |
| `src/format.c` | md/xml/json/txt emitters, tree renderer, escapers, manifest + outline rendering | 550 | 170 | 512 | `test_format.c` |
| `src/util.c` | strbuf, `str_dup`, lexical `/`-normalization, UTF-8→wide path open, UTF-8→UTF-16LE, FNV-1a | 360 | 242 | 301 | `test_util.c` |
| **total (`.c`)** | | | 1,710 | **4,394** | |
| **total (`.c`+`.h`)** | | **≤ 5,200 (NFR-09)** | 1,857 | **4,743** | |

**Current measurement (2026-10-17, after M9).** 3,752 lines against the 4,000
ceiling. `src/tokens.c` grew from 30 to 128 lines to carry the per-class
classifier and rates.

**M9 review outcome (owner decision, 2026-10-17).** Opt-in credential redaction
adds ~340 lines and lands at **3,752**, so NFR-09 is raised **3,500 → 4,000** on
the same "measured value + ~15 % headroom" rule used at M2, M3 and M8, rather
than trimming working, tested code. Two module caps move with it: `pack.c`
**460 → 520** (512 measured) and a new `src/redact.c` cap of **300** (255
measured). The justification is that both features were explicitly deferred from
v1 scope and then landed for an agent-driven audience: the manifest is what lets
a consumer identify what it received, and redaction removes an organisational
blocker for anyone sending a repo to a hosted model. The 300 lines are a
credential matcher, not speculative infrastructure.

**M10 review outcome (owner decision, 2026-10-18).** Outline mode adds **808**
lines (two new modules, plus rendering and wiring) and lands at **4,560**, so
NFR-09 is raised **4,000 → 5,200** on the same rule. Four module caps move with
it: `pack.c` **520 → 650** (566 measured), `format.c` **450 → 550** (479),
`cli.c` **500 → 520** (449), and a new `src/outline.c` cap of **620** (559
measured). The justification is the same shape as M9's: outline mode is the
feature that makes a repository-sized input usable in a 128 K window, which is
the agent-driven use case the whole tool exists for.

Two extractions paid part of the way. `src/fit.c` took `pack_fit()` out of
`pack.c` (512 → 482 at the time, freeing the headroom the feature needed), and
`src/outline.c` is new rather than grown. Neither was a refactor for its own
sake — `fit.c` existed because `pack.c` had 8 free lines.

**The LOC numbers are the interesting part of M10.** A pre-implementation guess
of 10× outline reduction on this repository's own source turned out to be
**10.8×** on `src/` — but only **3.9×** across the whole repository and **2.5×**
on an unrelated C++ codebase. The gap is prose: Markdown is refused by design, so
a documentation-heavy corpus sees almost nothing. Had the claim been published as
a single "10×" figure it would have been roughly right for code and badly wrong
for most real inputs, which is why the measurement — not the estimate — is what
the README carries.

**M11 review outcome (owner decision, 2026-10-18).** Truncation reporting adds
**183** lines and lands at **4,743**, still inside the 5,200 ceiling M10 set, so
no budget was raised. Two module caps move on their own review trigger:
`cmd.c` **240 → 300** (257 measured) and `read.c` **175 → 215** (186).

The trigger is worth stating plainly because the fix grew rather than shrank:
the reported bug was "two numbers disagree silently", and making them agree
honestly required distinguishing two causes, rendering that distinction in four
formats, and composing optional fields generally instead of ad hoc.

Three defects, only the first of which was the reported one:

- `bytes` came from `stat` while `lines`/`tokens` came from the truncated read,
  so a 14 MB file reported 14 MB beside counts derived from 4 MB — off by 3.4x,
  with no field saying so anywhere.
- `--max-file-tokens` was an `else if` after the read cap, so it was **silently
  ignored for every file at or above 4 MB**. A 4 MB read is ~1.2M estimated
  tokens, so asking for 2,000 got you ~1.2M.
- The token-cap marker was appended *after* cutting to exactly the cap, so the
  result exceeded the cap by the marker's own size. The marker is now built
  first and charged against the budget.

Fixing the second exposed a latent hang: the cut loop scanned backwards one
line at a time, re-estimating the entire prefix each step. That is quadratic,
and on a 4 MB file it never finishes — it had simply never been reached, because
the `else if` meant the loop only ever saw small files.

**M8 review outcome (owner decision, 2026-10-17).** The provenance manifest adds
158 lines and pushed `pack.c` from 451 to 502 against a 460 cap. The M7 review
had explicitly recorded that `pack.c` was the one module left with no headroom
and that "the next change there should be paid for by removing something", so
rather than raising the cap, **`pack_score` and its helpers were extracted to a
new `src/score.c`** — 58 lines, its own 80 cap, `test_pack.c` unchanged. That is
the same move that produced `glob.c` from `files.c`: lexical byte comparison is
not budget fitting. `pack.c` lands at **453/460**. No cap was raised in M8.

**M7 review outcome (owner decision, 2026-10-17).** The diff corrections and
`--patch` add 246 lines and push `files.c` from 448 to **555 against a 520 cap**
(+35). Applying the same standing rule — caps are measured value + ~15 %
headroom, and NFR-09 says an overrun "triggers a design review, not more code"
— `files.c`'s cap is raised **520 → 650**. `pack.c` lands at 451/460, within 9
lines, and is left alone deliberately: it is the one module with no headroom, so
the next change there should be paid for by removing something. The 246 lines
were kept rather than trimmed because they are corrections to shipped bugs (§12
D-09, D-10) plus two features with tests, not speculative code.

**M3 review outcome (owner decision, 2026-10-16).** M3 measures **3,075** lines
against the then-current NFR-09 ceiling of 3,000 — a 75-line (2.5 %) overrun,
concentrated in four modules whose M2 caps predate the M3 feature set:
`pack.c` (+142), `files.c` (+98), `config.c` (+90 against a 140-line estimate),
`format.c` (+38). The review applied this document's own standing rule — caps are
set to the measured value + ~15 % headroom — and raised NFR-09 **3,000 → 3,500**
rather than trimming working, tested code to fit a pre-implementation estimate.
Module caps in the table above were revised the same way. The overrun was not
scope creep: it is the cost of the multi-format emitter, git-native `diff`, and
config-file support (FR-009–FR-019), all delivered on the same LOC discipline.

`glob.c` was split out of `files.c` so pattern matching (independently
testable) stays separate from filesystem/git I/O.

Vendored libraries live in their own tree, fully separated from our code:
`vendor/cjson/` (cJSON, MIT), `vendor/getopt/` (NetBSD getopt/getopt_long,
BSD-2-Clause), `vendor/greatest/` (greatest.h, CC0). Each gets `LICENSE` and a
version tag; `vendor/PROVENANCE.md` records sources and retrieval dates.
Vendored files are never modified (portability shims are additive headers).

### 2.3 Data contracts between stages
- Paths are `/`-normalized, relative to the pack root, from discovery onward.
- `path_normalize()` is lexical: it converts both separators to `/`, collapses
  repeated slashes, drops `.` segments, resolves `..` against the preceding
  segment, and drops a trailing slash. A leading `/` (or the `//` of a UNC path,
  Windows only) and a `C:` drive prefix are preserved; `.` stays `.` and `""`
  stays empty. Symbolic links are never resolved. Because it always writes, it is
  only called on caller-owned buffers (never `argv` directly).
- The candidate list is sorted (FR-019) before fitting; nothing downstream reorders.
- `read` returns either content, `binary(size)`, `truncated(content)`, or an error
  (fail-fast per D-10).

## 3. Design decisions (ADR)

| ID | Decision | Status |
|---|---|---|
| D-01 | C standard: detect newest of C23 → C2x → C17 (MSVC: `/std:c23` → `/std:c17`); floor C17 = configure fatal | approved (TRS NFR-02) |
| D-02 | JSON via vendored **cJSON** (single module: `cJSON.c/h`) for config + JSON output | **approved 2026-10-16** by user ("vendored single file") |
| D-03 | argv via vendored **portable getopt**: NetBSD `getopt_long.c` (single file, BSD-2-Clause); musl rejected (depends on musl-internal headers) | approved 2026-10-16; impl chosen M1 |
| D-04 | Ignore semantics **delegated to git**: `git ls-files -c -o --exclude-standard`; non-git fallback = walker skipping `.git/` + user globs. No gitignore parser of our own | **approved 2026-10-16** by user |
| D-05 | Estimator = script-aware calibrated heuristic for v1; exact BPE **evaluated and rejected 2026-10-17** (model-specific, 2.3 MB, AV-blocking) — revisit only as a sidecar build | approved 2026-10-16, revised 2026-10-17 |
| D-06 | Test harness = vendored greatest.h + CTest; goldens committed | approved (P2) |
| D-07 | Determinism: byte-wise sort of normalized paths; no locale APIs (`strcoll` banned) | approved (P4) |
| D-08 | stdout=data, stderr=diagnostics | approved (P6) |
| D-09 | Windows single binary: static CRT (`/MT`) via `CMAKE_MSVC_RUNTIME_LIBRARY` | approved (NFR-04) |
| D-10 | I/O errors fail fast (exit 1); `--skip-unreadable` deferred post-v1 | approved 2026-10-16 |
| D-11 | Clipboard via `clip.exe` spawn for v1 (UTF-8 caveat documented); Win32 `CF_UNICODETEXT` post-v1 if needed | approved 2026-10-16 |

Rationale highlights:
- **D-02 rejected**: own parser (~400 LOC, violates P1) and jsmn (tokenizer only —
  we'd build the emitter and reader ourselves). cJSON gives parse *and* serialize.
- **D-04 rejected**: hand-rolled gitignore parser (subtle semantics, ~300 LOC, and
  still less correct than git itself).
- **D-03 rejected**: own argv parser (~150 LOC reinvention; getopt semantics are the
  expected CLI contract). Integration tests remain ours regardless.
- **D-03 impl**: NetBSD's single-file `getopt_long.c` (contains both `getopt` and
  `getopt_long`) over musl's pair — musl includes internal headers (`libc.h`,
  `syscall.h`) and would require modifying vendored code; NetBSD needs only
  additive shims (`vendor/getopt/getopt_compat.h` + `compat/` + `msvc-compat/`,
  force-included/scoped by CMake). Details: `vendor/PROVENANCE.md`.
- **D-09**: `/MT` avoids VCRuntime redistributable — "one file, double-click it".

## 4. Key algorithms

### 4.1 Discovery
```
if git work tree:  git ls-files -c -o --exclude-standard   (tracked + untracked, ignores honored)
else:              recursive walk, skip .git/, no dir-symlink recursion
filter:            --include (if set) then --exclude
sort:              FR-019 order
```
Git runs via `popen` with a **fixed command string** (`git rev-parse
--is-inside-work-tree`, then `git ls-files -c -o --exclude-standard -z`). The
child process `chdir`s into the pack root and the caller's cwd is restored
afterward, so no user path is ever interpolated into a shell command; NFR-08 is
satisfied by construction rather than by a metacharacter validator.

### 4.2 Budget fitting (deliberately simple, not clever)
```
reserve   = est(header) + est(full tree upper bound)      # conservative
remaining = budget - reserve
for f in sorted candidates:
    e = est(f)          # f is already read, and already truncated if
                        # --max-file-tokens applied (see 4.4)
    if e <= remaining:  include whole; remaining -= e
    else:               exclude (reason: over budget)
if nothing fits the reserve: exit 3
```
Conservative reservation keeps the invariant (FR-007) trivially true; the cost is
minor unused slack. Alternatives (iterative shrink-until-fits) considered and
rejected as not worth the complexity for v1.

Truncation is applied when each file is read (4.4), not during fitting, so the
fit only ever compares final, already-truncated estimates — one pass, no
re-reading. A file excluded here is reported with its reason.

### 4.3 Report assembly
Straightforward counting alongside 4.2: included/truncated/excluded-with-reason.
Auto-rendered whenever `--budget` is set (the report is the product's
differentiator); `--report` forces it when no budget is given.

### 4.4 Binary sniff & truncation
- Sniff: scan the first `min(len, 8192)` bytes; any NUL → binary. Placeholder line:
  `` `«binary: <path> (<N> bytes)»` ``.
- Read cap: at most 4 MB per file regardless of flags; if hit, the content is cut
  and the marker `` `… [truncated at <N> byte read cap]` `` is appended.
- `--max-file-tokens N`: cut at the last newline before the estimated cap, keeping
  the trailing newline, then append the marker `` `… [truncated at <N> est. tokens]` ``.

### 4.5 Tree renderer
Sorted paths, two-space indentation, **plain ASCII** connectors (`+--`, `|  `);
content = included files only. A unicode box-drawing option is post-v1.

### 4.6 Token estimator (spec + calibration record)

**Model (revised 2026-10-17).** Every code point is classified into one of eight
script classes and charged that class's own rate:

```
est = ceil( ( Σ_c  count_c · rate_c ) / 1000 )
```

| Class | Covers | rate (/1000) | tokens/char |
|---|---|---|---|
| `ascii` | ASCII, excluding `\n` | 264 | 0.264 |
| `newline` | `\n` | 1185 | 1.185 |
| `latin` | accented Latin (U+00C0–024F, 1E00–1EFF, …) | 1389 | 1.389 |
| `cyrillic` | U+0400–052F, 2DE0–2DFF | 377 | 0.377 |
| `cjk` | Han, kana, Hangul | 880 | 0.880 |
| `emoji` | pictographs, dingbats, VS-16, regional indicators | 2495 | 2.495 |
| `marks` | combining-mark scripts (Devanagari, Thai, …) | 982 | 0.982 |
| `other` | Greek, Arabic, Hebrew, unlisted | 884 | 0.884 |

Rates are integers, so the whole expression is integer arithmetic with no locale
use — bit-identical on every platform (P4). Inputs are bounded by the read cap,
so the products cannot overflow 64 bits.

**Why it changed.** The previous model was `ceil((10·C + 35·L)/38)`, which charged
*every* code point the ASCII rate of 0.263. Measured against tiktoken on this
repository that is fine for ASCII source and catastrophic elsewhere, because one
Han character costs roughly one whole token:

| Content | old estimate | cl100k | old error |
|---|---|---|---|
| ASCII source | — | — | median +5% |
| CJK prose | 0.26 /char | ~1.0 /char | **−41%** (worst −85%) |
| accented Latin | 0.26 /char | ~1.1 /char | **−34%** |
| emoji | 0.26 /char | ~2.0 /char | **−63%** |

Under-estimating means `--budget` silently overflows the model's context window,
which is the failure mode that matters.

**How the rates were derived (2026-10-17).** For each non-ASCII class, samples
containing *only* that class were tokenized with both `cl100k_base` and
`o200k_base`; the ASCII and newline rates were subtracted, and the residual
divided by the class's code-point count. The **median** across samples and both
encodings was taken, then multiplied by **1.25** and rounded up. The bias is
deliberate: over-estimating wastes a little of the user's budget,
under-estimating produces a failed request.

A least-squares fit over the full feature set was tried first and rejected — it
produced negative rates for `other` and 7.3 tokens/char for `emoji`, because the
synthetic per-script samples are too few and too collinear. Splitting ASCII into
letters versus punctuation was also measured and rejected: it improved the median
but doubled p90 (+36% vs +15.7%). Both dead ends are recorded so they are not
re-attempted.

**Measured result** — same corpus, both encodings, coefficients *not* fitted on
the repo files (derivation used synthetic single-class samples only):

| | median | p90 | worst | under-estimates |
|---|---|---|---|---|
| old model | −0.1% | +21.2% | **−85.6%** | 75/146 |
| **new model** | **+4.4%** | +36.4% | **−37.0%** | 47/146 |

Per class (median): ascii +5%, cjk **+32%**, cyrillic +19%, emoji +24%,
latin +27%→+24%, other −0%. Every non-ASCII class now errs on the safe side;
none of them under-estimates at the median any more.

End-to-end on this repository, per file, against real tiktoken counts:
**52 files, aggregate +11.61%** vs `cl100k_base`, worst single file **−26.1%**
(`src/tokens.c`, punctuation-dense C).

**Limitations (stated, not hidden).**
- **Punctuation-dense ASCII under-estimates by up to ~26%.** `src/tokens.c`
  (braces, commas, short identifiers) is the worst case. This is pre-existing
  behaviour, not a regression from this change; splitting ASCII to fix it was
  measured and made the overall spread worse.
- **Dense macro code over-estimates by up to +55%** (`vendor/greatest/greatest.h`).
- **Combining-mark scripts are the weakest class** (`marks`): the residual
  attribution inflates it, so Devanagari/Thai text over-estimates ~+113%.
- A character-count model cannot reach exactness without a real tokenizer, which
  would need a ~2.3 MB vocabulary (tried, rejected — see §12).
- This is a heuristic, not a tokenizer. Outputs keep the `est.` label.
- **Corpus**: the repository's first-party text files (source, tests, docs,
  tools, root). Vendored third-party code is excluded from *fitting*: its token
  density is unrepresentative and it dominated the token mass. Vendored code is
  still present in the end-to-end validation above, which is why it shows up as
  the +55% over-estimate case.
- **Reference tokenizer**: tiktoken `cl100k_base` (GPT-4/3.5), cross-checked on
  `o200k_base` (GPT-4o), 0.14.0.
- **Reproduce**: `python tools/calibrate_tokens.py` (re-derives the per-class
  rates from the same synthetic single-class samples and reprints the table
  above).
- **Three-corpus benchmark (M7, 2026-10-17)**: `python tools/benchmark.py <repo>
  ...` measures end-to-end accuracy against `cl100k_base` and writes
  `tools/benchmark-results.md`. Two rules it enforces, both learned the hard way:
  estimates come from **the real binary** via `count --format json`, never from a
  Python reimplementation of the classifier (an earlier investigation rebuilt it
  and reported a −54% worst case the binary does not reproduce — it diverged on
  one file, while two of its other statistics matched exactly, which is what made
  the wrong number look plausible); and **binary files are excluded**, because
  cctxpack does not estimate them (an uncorrected harness scored a
  `__pycache__/*.pyc` at −61% and a `blob.bin` at −54%, inventing a worst case
  the tool never produces).

  | Corpus | Files | Aggregate | Median | p90 | Worst | Under |
  |---|---:|---:|---:|---:|---:|---:|
  | C++ | 99 | +24.60% | +32.21% | +52.00% | −2.45% | 1.0% |
  | C (this repo) | 64 | +10.40% | +5.59% | +36.77% | −26.11% | 26.6% |
  | Markdown | 336 | +30.20% | +31.39% | +45.96% | −17.32% | 2.7% |

  The third corpus (prose-heavy Markdown) was added because every conclusion up
  to this point rested on two repositories, one of them this project — the
  weakest link in the whole calibration record, and flagged as such twice. Two
  findings came out of it. **Prose over-estimates about 6 points more than
  code** (+31.7% vs +32.2% median on the dominant class, but +30.2% aggregate
  against +24.6% for C++, with far longer lines): English is dense in short
  common words that BPE merges, so a per-character rate runs high. And the
  **CJK evidence remains thin at 9 files** — the +22% median is the least
  trustworthy number published here.
- **Previous calibration (2026-10-16, superseded)**: least squares on
  `T = p·C + q·L` gave α = 3.403/3.615 and β = 3.856/3.865, rounded to
  α = 3.5, β = 3.8. It recorded a +2.07% corpus aggregate, but that aggregate
  **hid the real failure**: it is dominated by ASCII files and cancels
  over-estimates against under-estimates. Re-measuring per content class on the
  same corpus showed **75 of 146** observations under-estimating and a **−85.6%**
  worst case, which the new model cuts to 47 and −37.0%. **Aggregates are not a
  sufficient acceptance metric for an estimator; the per-class distribution is.**

The fitting invariant (the bundle's own estimate never exceeds the sum of the
part estimates counted while fitting) holds because `est` is the ceiling of an
additive linear form, and is checked by `test_tokens.c`.

## 5. Output format contracts (grammar level)

`format_id` is a stable enum used across the codebase and never re-ordered:
`FMT_MD = 0`, `FMT_XML`, `FMT_JSON`, `FMT_TXT`. `format_parse()` maps `--format`
names and returns `-1` for anything else (the CLI turns that into a usage error,
exit 2). All four formats share the same five-stage emitter (`format_open` ->
`format_tree` -> `format_files_open` -> `format_section` / `format_binary` …
-> `format_close`), so adding a format is a switch arm, not a second code path.

### 5.1 Common invariants
- `format_open` emits the root display + `CTX_VERSION` for every format. **md**
  and **txt** carry the version in a header line; **xml** and **json** carry it
  as a `version` attribute/key so machine consumers can detect the schema.
- The `first` parameter only controls separators (the JSON array comma, the
  blank line before each non-first XML/txt entry). Per-file **estimates are
  always rendered with `first = 0`**, so a file's cost never depends on its
  position (FR-007); the emitting pass passes its own counter.
- Per-file `tokens` is the **content-only** estimate (`tokens_estimate(content)`),
  not the rendered section size. Budget fitting uses the rendered section
  estimate (`est`), which includes the path/fence chrome.
- Binary files carry **no content** in any format; only path + byte size.

### 5.2 Per-format grammar
- **md** — `# Context bundle: <root>` header plus a
  `<!-- generated by cctxpack <V>; token counts are estimates -->` comment. The
  tree is a fenced `text` block. Per file: `### <path>` then a fenced block
  whose fence length **widens past the longest backtick run in the content**
  (`fence = max_run >= 3 ? max_run + 1 : 3`) so content can never close the
  block early. Language hint from the file extension.
- **xml** — `<?xml version="1.0" encoding="UTF-8"?>` + `<context root="…"
  version="…">`, optional `<tree>`, then `<file path="…" tokens="…">content</file>`
  (`binary="true" bytes="…"` self-closing for binaries) and `</context>`. Paths
  and content pass through a tested escaper (`& < > " '`); content missing a
  trailing newline gets `&#10;` re-appended so the close tag starts its own line.
- **json** — streamed, never materialized as one tree:
  `{"root":…,"version":…,"tree":[…],"files":[…]}`. The tree is an **array of
  path strings** (machine-readable, not drawn ASCII). Each file object is
  `{"path":…,"tokens":…,"content":…}` or `{"path":…,"binary":true,"bytes":…}`.
  String escaping is delegated to vendored cJSON so quoting follows the library's
  rules instead of a second hand-rolled escaper.
- **txt** — minimal, for piping to a model without Markdown overhead:
  `cctxpack bundle: <root> (cctxpack <V>)` header, the tree in plain ASCII
  connectors, then `===== <path> =====` before each file's content.

Final goldens pin the exact bytes for all four formats.

## 6. Error handling policy

Exit codes are part of the public contract: `0` success, `1` runtime failure,
`2` usage/config rejection (nothing was packed), `3` budget infeasible.

| Condition | Action | Exit |
|---|---|---|
| unknown option / missing option value | usage on stderr + `try 'cctxpack --help'` | 2 |
| `--budget` / `--max-file-tokens` not a number | usage on stderr | 2 |
| `--format` not one of `md`/`xml`/`json`/`txt` | usage on stderr | 2 |
| `--sort` not one of `name`/`mtime`/`size` | usage on stderr | 2 |
| empty `--query`, empty `--include`, empty `--exclude` glob | usage on stderr | 2 |
| `--clipboard` on non-Windows | `only available on Windows` | 2 |
| `--clipboard` together with `-o` | mutually exclusive sinks | 2 |
| `--staged` without the `diff` command | usage on stderr | 2 |
| `diff` given explicit path operands | `diff takes no paths` | 2 |
| path operand does not exist | `no such path: <arg>` | 2 |
| invalid `.cctxpack.json` | stderr with cJSON error offset | 2 |
| `.cctxpack.json` key has the wrong type | stderr naming key + wanted type | 2 |
| `diff` outside a git work tree | `diff needs a git work tree` | 2 |
| out of memory | `out of memory` | 1 |
| git command fails | stderr + git's own message | 1 |
| file read failure (fail fast, D-10) | `cannot read <path>` | 1 |
| budget infeasible (nothing fits) | stderr report | 3 |
| success | bundle on stdout, `-o` file, or clipboard | 0 |

Every usage error names the offending flag/value and is followed by the
`try 'cctxpack --help'` hint. Configuration is validated before any file is
read, so a rejected config never leaves a partial bundle behind.

## 7. Testing strategy

- **L1 unit** — per-module greatest.h suites (glob matrix, estimator goldens, config
  precedence, fit invariants: output ≤ budget; include/exclude inverses; binary
  sniff; path normalization both separators).
- **L2 e2e** — the CTest `unit` run also executes the real binary (`$<TARGET_FILE:cctxpack>`)
  against `tests/fixtures/repo` covering pack, budget, determinism, `-o` guard, and
  usage errors, asserting on the emitted bundle (substrings + the budget invariant)
  and on exit codes. Standalone golden files are not used; the fixture repo is the
  golden *input*. Git-ignore semantics are proved by a throwaway work tree the test
  creates at runtime (`git init` + one tracked, one ignored file), so the check does
  not depend on the enclosing repository and degrades to `SKIP` when git is absent.
  All e2e artifacts go to a `ctx_e2e_tmp/` scratch dir created and removed around the
  suite, so runs never leak files into the source tree.
- **L3 invariants** — pack twice, byte-compare (determinism, NFR-07); budget never
  exceeded for random budgets on the fixture corpus (in C, seeded PRNG).
- **L4 platform CI** — matrix: windows-msvc (Ninja), windows-mingw (msys2 action),
  ubuntu gcc + clang, macos clang. Git-dependent tests create their own temp
  repository and `SKIP` (greatest `SKIPm`) when git is absent.
- Windows-specific: UTF-8 filename pack test (exercises wide-path open in util).

## 8. Build & release

- One target (`cctxpack`) + tests (added M1). Version single-sourced from
  `project(VERSION …)` via `target_compile_definitions(CTX_VERSION=…)`.
- C standard detection loop lives in `CMakeLists.txt` (D-01), status printed.
- Strict ISO mode (`-std=c23`) hides POSIX declarations on glibc, so non-Windows
  builds define `_DEFAULT_SOURCE` (needed for `popen`, `lstat`, dirent `d_type`,
  `getcwd`). The macro is inert on Windows and Apple toolchains.
- Release: tag `v*` → CI builds 4 platform binaries, strips, attaches with SHA-256
  checksums. No CPack in v1.

## 9. Security & robustness

- Content is data, never executed. Git/clip are the only spawned processes: fixed
  argv strings, user input validated (no shell metacharacters), `--` separators.
- No directory-symlink recursion (prevents loops and escapes by construction).
- Read cap per file (default 4 MB absolute, even without `--max-file-tokens`) with
  explicit truncation marker — no memory blowups on pathological inputs.
- Hostile-path test cases in `test_files.c` (quotes, `%`, newlines in names).

## 10. Risks & mitigations

| Risk | Mitigation |
|---|---|
| Estimator accuracy criticized | honest `est.` labeling, published calibration (§4.6), exact tokenizer as post-v1 option |
| "cctxpack" weak name recognition | name decided 2026-10-16; README title "cctxpack — pack codebases for LLM prompts", topics + launch posts carry discovery |
| Scope creep (MCP, templates, …) | TRS §8 is the gate; new features = new approved requirements |
| Competitor feature-parity (budget mode) | launch timing favors us; correctness + speed are the moat |
| MSVC lacks `/std:c23` | detection floor C17 (D-01); C23 used where available |
| Windows UTF-8 paths | wide-path open in util + dedicated test (§7) |

## 11. Post-v1 extensions (all approval-gated)
Exact BPE tokenizer (D-05 follow-up), MCP server, secret redaction, remote URL
packing, `--skip-unreadable`, shell completions, scoop/brew taps, unicode tree option (`--unicode`).

## 12. Decisions log & remaining open items

Resolved 2026-10-16 (owner decisions):
- D-03 vendored getopt — approved; D-05 heuristic estimator — approved;
  D-10 fail-fast I/O — approved; D-11 clip.exe clipboard — approved.
- getopt impl (M1): NetBSD `getopt_long.c` vendored; musl rejected (internal
  headers would force modifying vendored files).
- Config discovery: walk up to git root; cwd-only fallback outside a work tree.
- `--budget` unset = pack everything; report auto-fires whenever a budget is set.
- Tree: plain ASCII from day one. `diff`: unstaged default + `--staged`.
- `count --format json` stays in v1 (FR-011). License MIT. Name: **cctxpack**.

**Superseded by M7 (2026-10-17) — see D-09 below.** The line above recorded
`diff` as "unstaged default", which was the bug: it compared the work tree to
the *index*, so a fully staged change set produced no output at all.

Resolved 2026-10-17 (M7 diff corrections and features):
- **D-09 `diff` uses `git status --porcelain -z`, not `git diff --name-only`.**
  Three silent gaps are fixed: untracked files were invisible (git diff cannot
  see them), a fully staged work tree returned nothing, and `--untracked-files`
  must be `all` because the default collapses an untracked directory to a single
  `dir/` entry that cannot be read. The default is now everything uncommitted;
  `--staged` remains the index alone. Renames contribute their new path and the
  old-path field is skipped; deleted paths are dropped.
- **D-10 `--patch` passes git's diff through verbatim** instead of splitting it
  into per-file sections. Splitting would mean parsing git's `+++ b/<path>`
  headers, and git quotes paths containing unusual bytes — matching those back
  to our own path list is where such parsers break. One `git diff -U3 HEAD`
  call, no path interpolation into a shell (so no injection surface), and the
  emitted section is a valid patch that can be saved and applied. Untracked
  files have no diff representation and so are absent from `--patch` by
  construction.
- **D-11 `.cctxpackignore` is layered on `.gitignore`, not a replacement**, and
  is implemented by feeding its globs through the existing `glob_filter` rather
  than adding a second matcher.
- **D-12 `--no-tree --format json` was shipping invalid JSON.** `format_open`
  writes the `"tree"` key and nothing closed it when the tree was suppressed,
  yielding `{"tree":,"files":...`. No test covered the combination.
  `format_tree` is now called unconditionally with an empty list, which renders
  `[]`. Found while wiring `--patch`, which hit the same wall.
- **`--delta <saved bundle>` was dropped rather than half-built.** It needs the
  previous bundle's per-file token counts, which means parsing our own output
  format back. The useful half of it — "how much cheaper is this than whole
  files" — is reported by `--patch` itself, so the remaining gap is only
  bundle-vs-bundle comparison. Deferred as an explicit decision, not an
  oversight.
- **`cctxpack -` stdin mode was dropped for budget reasons**: `cmd.c` had 38
  lines of headroom and it was the lowest-value item of the four.

Resolved 2026-10-17 (M8 provenance manifest, agent-driven audience):
- **D-13 every bundle carries a manifest**, giving a consumer enough to say
  exactly what it received: commit SHA, file count, content bytes, content
  tokens, and a digest. Rendered per format — HTML comment (md, machine
  greppable and invisible when rendered), attributes on `<context>` (xml), a
  nested `"manifest"` object (json), a `manifest:` line (txt).
- **D-14 the manifest contains NO wall-clock time.** Byte-identical output for
  identical inputs is a stated property of this tool and a test; a timestamp
  would have silently broken it. Every field is derived from the input, so two
  runs over an unchanged tree agree exactly. `e2e_manifest_is_deterministic`
  pins this.
- **D-15 `digest` is FNV-1a-64 over each included path plus a NUL, in emission
  order** — it identifies the file set and its ordering, and is recomputable by
  anyone holding the JSON output. It is deliberately **not** a content-integrity
  hash and is documented as such; that job belongs to the release checksums.
- **D-16 the manifest is rendered after the budget fit** (it reports fitted
  totals), so the reserve uses a probe header built with `SIZE_MAX` for every
  number. Those render the widest possible digits, so the probe is never shorter
  than the real header and a bundle cannot overshoot its budget by its own
  header. The real header's estimate is then added to the reported total, so
  `est. tokens X/Y` is what the bundle actually costs.
- **`score.c` extracted from `pack.c`** (D-16 follow-on): the M7 review had
  committed to paying for `pack.c`'s next change by removal rather than by
  raising its cap, and `pack_score` is an independently testable concern.

Resolved 2026-10-17 (M9 opt-in credential redaction, agent-driven audience):
- **D-17 `--redact` is opt-in, and that is not a default-setting detail — it is
  the feature.** This tool exists to hand a model the exact bytes of a
  repository; silently rewriting content would break its core promise, so the
  only feature that alters content must be asked for by name. NFR-13 was added to
  state that as a requirement rather than leave it to judgement.
- **D-18 no entropy heuristic, deliberately.** Length-and-alphabet scoring is
  how redactors end up mangling SHA digests, base64 assets, lockfile integrity
  hashes and minified bundles, and it still misses most real keys because those
  keys live in environment variables rather than in source. Every rule is a
  fixed vendor-issued shape with a minimum length. The cost is a stated blind
  spot rather than silent damage.
- **D-19 placeholder filtering is what makes assignment matching survivable.**
  Without it, `--redact` would destroy examples, docs and test fixtures — the
  common case in a repository, not the rare one. `your-…`, `${VAR}`, `<…>` and
  `changeme` are skipped, as is anything under 6 bytes, and a name that is the
  tail of a longer identifier (`password_hash`) does not match `password`.
- **D-20 replacement covers only the secret span, and preserves the line count.**
  An earlier version replaced from the variable NAME through the value, which
  rendered `db_password = "…"` as `db_[REDACTED:password]"` — an unbalanced quote
  and a statement the model could not read. Consuming `name = value` while
  emitting the marker over `value` alone keeps the source parseable. Newlines
  inside a match are re-emitted, so a collapsed multi-line key block still
  occupies the lines it did: a consumer given line numbers keeps valid ones.
- **D-21 the manifest's `redactions` field is per-format.** It is a quoted XML
  attribute, a JSON key, and bare text in md/txt. Sharing one fragment across
  formats produced `"digest":"…",redactions=2}` and an unquoted XML attribute,
  i.e. unparseable output in two of four formats. Omitted entirely when zero, so
  an unscrubbed bundle carries no field claiming it might have been scrubbed.
- **D-22 declarations are recognised at the moment a `{` opens, not at the end
  of the line.** `int f(void) { ... }` written on one line never has depth > 0
  when the line ends, so an end-of-line rule misses it entirely; and a signature
  split across two lines has its `{` on a line containing nothing but the
  brace, which is why the scanner also falls back to the previous line. Getting
  this wrong made every single-line function invisible — the most common shape
  there is.
- **D-23 the scanner refuses rather than guesses.** Unbalanced braces (the scan
  lost sync), an extension outside the claimed set, or a substantial file that
  yields nothing recognisable all return `-1`, and the caller emits the **full
  text**. A confident wrong outline is worse than no outline: to the consumer it
  is indistinguishable from a correct one. Measured refusal rate on an unrelated
  C++ codebase: 16 of 99 files, of which 15 are Markdown and one is a C++ test
  file.
- **D-24 brace-less files get their own pass.** A header of prototypes has no
  braces at all, which the depth rule would reject — but prototypes *are* the
  declarations worth listing, and headers are a large share of a repository's
  file count. `prototype_outline()` collects includes and `;`-terminated
  signatures. This took the repo's own refusal count from 2/29 to 0/29.
- **D-25 progressive disclosure is a single pass, not two.** The first design
  spent the budget on whole files, then spent whatever was left on outlines. It
  was useless: the first pass always consumed the entire budget, so
  `--outline --budget N` produced the same output as `--budget N`. Deciding per
  file — whole body if it fits, else outline, else dropped — is what makes the
  repository stay representable while the most relevant files still arrive
  complete. Measured at `--budget 6000` on `src/`: 2 files whole, 10 outlined,
  5,966/6,000 tokens.
- **D-26 JSON outlines are structured, not text.**
  `"outline":[{"line":12,"decl":"..."}]` rather than a pre-rendered blob,
  because JSON is the machine path and a consumer can select from an array but
  not from prose. `count --outline` routes through the same helper `pack` uses
  (`count_of`), so the two commands cannot silently disagree — the same reason
  `--redact` is rejected where it would be a no-op.
- **D-27 `bytes` keeps meaning "on disk", and `bytes_packed` is added rather
  than `bytes` being redefined.** The two numbers describe different things
  once truncation happens, and only one of them can keep the familiar name.
  `bytes` is the one a caller already has (file listings, git, `ls`) and
  changing its meaning would silently break consumers. `bytes_packed` is emitted
  only when it differs, so a bundle that lost nothing is byte-identical to one
  built before this change — the same "no field claiming what did not happen"
  rule that `redactions` follows (D-21).
- **D-28 the truncation reason is part of the interface, not just a boolean.**
  The read cap is a memory bound and the token cap is a request the user made;
  they have opposite remedies, so a single `truncated: true` cannot be acted on.
  `truncation` is one of `read-cap` or `token-cap`. Where both could apply, the
  token cap is reported because it is the binding constraint and the one that
  explains the result; the read cap still bounded memory but cut no further.
- **D-29 the token cap cuts before the marker is added, and the marker is
  charged against it.** Cutting to exactly `max_tokens` and then appending the
  marker overshoots by the marker's own size (~13 tokens). The marker's text is
  known up front because `max_tokens` is, so its cost is subtracted first. Small,
  but it is the one guarantee the tool makes, and a cap that is exceeded by the
  tool's own explanation of the cap is not a cap.
- **D-30 the token-cut search is a binary search.** `tokens_estimate` is a plain
  non-decreasing sum over the prefix — no length normalisation — so the cut
  point is found in O(log n) estimates and then rounded back to a line boundary.
  The previous backward line scan re-estimated the whole prefix per line, which
  is quadratic: on a 4 MB file it does not terminate. It was never reached
  because the `else if` in D-28's predecessor confined it to small files, so
  fixing the `else if` is what exposed the hang. The two bugs were the same
  line of code.

Resolved 2026-10-16 (M2 code review):
- **Estimator constants α = 3.5, β = 3.8**, evaluated as `ceil((10C+35L)/38)` in
  integer arithmetic for cross-platform determinism. Full record in 4.6.
- **LOC caps revised** to measured values + ~15 % headroom and the total ceiling
  raised 1,800 → 2,000 (to cover the not-yet-written `config.c` and the `glob.c`
  extraction). Rationale: pre-implementation estimates under-counted the flat
  CLI/path-handling and walker code; no real complexity was added. See 2.2.
- **`glob.c` extracted** from `files.c` (separate, independently testable
  concern: pattern matching vs filesystem I/O).
- **git discovery** uses chdir + fixed command strings (no path interpolation),
  replacing the metacharacter validator in the earlier draft (4.1).
- **Truncation moved to the read stage** (4.4), keeping budget fitting single-pass
  (4.2).
- **Path-argument semantics**: 0 args → cwd; 1 directory → that root; 1 file →
  its dir + exact-file filter; ≥2 args → cwd root with prefix/exact filters;
  absolute operands rejected in multi-arg mode (exit 2).
- **Unimplemented v1.0 surface** (`count`, `tree`, `diff`, `--format` ≠ md,
  `--sort` ≠ name, `--query`, `--clipboard`) is recognized and exits 2 with a
  "planned M3/M4" message rather than being silently ignored.
- **Path normalization is lexical** (`path_normalize`, 2.3): it now also drops
  `.`/empty segments and resolves `..`, so `./x` and `x` compare equal on POSIX.
  This fixed the `-o` overwrite guard on macOS, where the guard's absolute path
  (`join(root=".", "README.md")` → `"./README.md"`) did not match the input's
  absolute path. Because the function always writes, it is never applied to
  `argv` directly — globs are normalized on a duplicate (`push_norm`), which also
  removes a latent write-to-`argv` (crash on read-only literals).
- **`_DEFAULT_SOURCE`** is defined for non-Windows builds (`-std=c23` hides
  POSIX declarations on glibc); Windows/MinGW and Apple toolchains are unaffected.

Resolved 2026-10-16 (M3 code review, commit `8317a60`):
- **Four output formats** behind one five-stage emitter (§5). `format_id` order is
  frozen (`FMT_MD = 0` first) because it is persisted-facing; `--format` names are
  parsed by a single `format_parse()`.
- **Markdown fence widening** (§5.2): the fence grows past the longest backtick
  run in the content instead of using a fixed 3/4-backtick heuristic. Fixed width
  is wrong for markdown inside markdown (e.g. this document), which silently
  truncates a bundle or breaks the block.
- **JSON tree is an array of path strings**, not drawn ASCII — the machine formats
  are meant to be parsed, and re-deriving a tree from paths costs the consumer
  nothing.
- **One comparator for all ordering**: `pack_item` carries `key`, `key2`, `path`;
  sorting is always `(key, key2, byte-wise path)`. Plain sorts set
  `key = key2 = <sort-mode key>`; `--query` sets `key = -pack_score(path, query)`
  and `key2 = <sort-mode key>`, so relevance wins and the requested sort still
  breaks ties. This removed the previous separate rank-then-sort path and made
  determinism a property of the comparator alone (NFR-07).
- **`pack_score` is lexical, not semantic**: per-term ASCII case-insensitive
  non-overlapping counts, basename hits weighted ×2, ties broken byte-wise on the
  path. It never opens a file, so re-ranking a 50k-path repo stays instant and
  free of I/O error paths. Semantic retrieval is post-v1 (would need a dependency
  or a built-in embedding — out of budget).
- **`diff` reuses git's own filter** (`--name-only --diff-filter=ACMR -z`, plus
  `--cached` for `--staged`) from `git rev-parse --show-toplevel`. NUL splitting
  avoids the newline-in-filename ambiguity; `-z` is why this is Unix-first —
  on Windows git still emits NULs, so the read is portable.
- **`diff` defaults the tree off** (FR-010): a change review wants the diff, not
  a full-repo map that would eat the budget.
- **Config precedence**: nearest `.cctxpack.json` at or above cwd, bounded by the
  git work-tree root (cwd-only outside a work tree); CLI always wins; an invalid
  or wrongly-typed config is a hard error with the cJSON offset, never a silent
  fallback.
- **Clipboard is UTF-16LE + BOM**, matching what `Get-Clipboard`/most Windows
  consumers expect from a raw CF_UNICODETEXT write; malformed UTF-8 becomes
  U+FFFD rather than aborting the copy.
- **cJSON is now compiled and linked** (it was vendored but unused through M2);
  it backs config parsing, `--format json` string escaping, and `count --format json`.

- **NFR-09 raised 3,000 → 3,500** (owner decision, 2026-10-16). M3 measured 3,075
  first-party lines, 75 over the old ceiling. The review applied the doc's own
  "measured + ~15 % headroom" rule instead of trimming tested code to fit a
  pre-implementation estimate; module caps in §2.2 were revised the same way.
  Recorded per NFR-09's own escalation clause, not as silent drift.

Remaining open:
1. `vendor/cjson` is pinned to 1.7.19; no upgrade cadence is set. Re-check
   advisories before the release tag.
2. **The `--query` ranking is lexical, not semantic.** `pack_score` never reads
   file content, so it cannot tell that `parse_config()` is the right hit for
   "config parsing". A user who expects semantic search will be disappointed;
   the README should keep saying "keyword" rather than "relevance" until an
   embedding approach is approved (post-v1, needs a dependency).
3. **A fixed ~160 ms of Windows startup cost is three `git` spawns** (config
   discovery + work-tree probe + `git ls-files`), not I/O — see the benchmark
   breakdown in the README. Caching the work-tree root would remove one spawn;
   not done, since it is an optimization rather than a requirement.

Resolved 2026-10-17 — **exact BPE rejected, script-aware estimator shipped**:
- **D-05 superseded.** An exact BPE tokenizer was fully implemented (byte-level
  merge, hand-written pretokenizer state machines for both encodings, ~630 LOC)
  and then **deleted without shipping**. Reasons, in order of weight:
  1. **It was model-specific.** `cl100k_base`/`o200k_base` cover OpenAI models
     only. Anthropic does not publish its tokenizer, so the majority of
     "pack my repo for an LLM" users would get nothing from 2.3 MB of data.
     Even the two OpenAI encodings disagree by 67 points on Cyrillic.
  2. **Cost.** 2.3 MB of vendored vocabulary, a binary data format, a build-time
     code generator, a runtime path search, and a release-layout requirement, to
     feed the same counter more precisely. Binary went 245 KB → 2.6 MB.
  3. **It tripped Windows Defender.** Every binary embedding the blob was
     flagged `Trojan:MSIL/ClipBanker.GE!MTB` (an ML verdict — `MSIL` is .NET IL
     and this is native C). A locked-file remediation deadlock pegged CPU at
     100% and thrashed the disk until the binary was deleted manually. Shipping
     binaries that users' antivirus quarantines on download is a launch blocker.
  4. **It was already over budget** (NFR-09) and unfinished (a merge-loop bug
     remained).
  The written code, the vocab blobs, and the generator were removed rather than
  left in the tree. Sidecar files would be the delivery mechanism if this is ever
  revisited; the pretokenizer regexes are published in tiktoken's `encodings.py`.
- **Script-aware estimator shipped instead** (§4.6): eight classes, integer
  rates, median × 1.25 bias. Fixes the CJK −41% / −85% failure for ~113 LOC and
  zero bytes of shipped data. Remaining weakness is documented, not hidden:
  punctuation-dense ASCII still under-estimates by up to ~26%.
- **Lesson recorded: an aggregate accuracy figure is not an acceptance metric
  for an estimator.** The old +2.07% corpus aggregate concealed 69/124
  under-estimates and a −85.6% worst case, because ASCII dominates the corpus and
  cancels the errors. Future calibration must report the per-class distribution
  and the under-estimate tail.
- **Lesson recorded: do not derive coefficients from the validation corpus.**
  Early attempts fit on the repo files, and stray symbols with a count of 1–2
  (an arrow, a section sign) produced rates like 203,010 tokens/char. Derivation
  uses synthetic single-class samples only; the repo files are validation.
