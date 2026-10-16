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

| Module | Responsibility | LOC cap | Unit tests |
|---|---|---|---|
| `src/main.c` | command dispatch, version | 100 | e2e smoke |
| `src/cli.c` | getopt wrapper, validation, `--help` text | 200 | `test_cli.c` |
| `src/config.c` | `.cctxpack.json` load (cJSON), precedence merge | 150 | `test_config.c` |
| `src/files.c` | `git ls-files` runner, fallback walker, glob matcher | 250 | `test_files.c` |
| `src/read.c` | open/read, binary sniff, size caps, truncation | 130 | `test_read.c` |
| `src/tokens.c` | heuristic estimator | 120 | `test_tokens.c` |
| `src/pack.c` | ordering, budget fit, report assembly | 250 | `test_pack.c` |
| `src/format.c` | md/xml/json/txt emitters, tree renderer | 350 | `test_format.c` |
| `src/util.c` | strbuf, `/`-normalization, UTF-8→wide path open | 200 | `test_util.c` |
| **total** | | **≤ 1,800** | |

Vendored libraries live in their own tree, fully separated from our code:
`vendor/cjson/` (cJSON, MIT), `vendor/getopt/` (portable getopt, MIT),
`vendor/greatest/` (greatest.h, CC0). Each gets `LICENSE`, version tag, and a
provenance note; vendored files are never modified.

### 2.3 Data contracts between stages
- Paths are `/`-normalized, relative to the pack root, from discovery onward.
- The candidate list is sorted (FR-019) before fitting; nothing downstream reorders.
- `read` returns either content, `binary(size)`, `truncated(content)`, or an error
  (fail-fast per D-10).

## 3. Design decisions (ADR)

| ID | Decision | Status |
|---|---|---|
| D-01 | C standard: detect newest of C23 → C2x → C17 (MSVC: `/std:c23` → `/std:c17`); floor C17 = configure fatal | approved (TRS NFR-02) |
| D-02 | JSON via vendored **cJSON** (single module: `cJSON.c/h`) for config + JSON output | **approved 2026-10-16** by user ("vendored single file") |
| D-03 | argv via vendored **portable getopt** (MIT) | approved 2026-10-16 |
| D-04 | Ignore semantics **delegated to git**: `git ls-files -c -o --exclude-standard`; non-git fallback = walker skipping `.git/` + user globs. No gitignore parser of our own | **approved 2026-10-16** by user |
| D-05 | Estimator = calibrated heuristic for v1; exact BPE tokenizer = post-v1, approval-gated | approved 2026-10-16 |
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
- **D-09**: `/MT` avoids VCRuntime redistributable — "one file, double-click it".

## 4. Key algorithms

### 4.1 Discovery
```
if git work tree:  git ls-files -c -o --exclude-standard   (tracked + untracked, ignores honored)
else:              recursive walk, skip .git/, no dir-symlink recursion
filter:            --include (if set) then --exclude
sort:              FR-019 order
```
Git runs via `popen` with a **fixed command string**; user paths pass through a
validator that rejects shell metacharacters (NFR-08) rather than escaping them.

### 4.2 Budget fitting (deliberately simple, not clever)
```
reserve  = est(header) + est(full tree upper bound)      # conservative
remaining = budget - reserve
for f in sorted candidates:
    e = est(f)
    if e <= remaining:        include whole; remaining -= e
    elif max_file_tokens set and e > max_file_tokens and min slice fits:
                              include truncated; remaining -= slice
    else:                     exclude (reason: over budget)
if nothing included that fits the reserve: exit 3
```
Conservative reservation keeps the invariant (FR-007) trivially true; the cost is
minor unused slack. Alternatives (iterative shrink-until-fits) considered and
rejected as not worth the complexity for v1.

### 4.3 Report assembly
Straightforward counting alongside 4.2: included/truncated/excluded-with-reason.
Auto-rendered whenever `--budget` is set (the report is the product's
differentiator); `--report` forces it when no budget is given.

### 4.4 Binary sniff & truncation
- Sniff: read first 8192 bytes; any NUL → binary. Placeholder line:
  `` `«binary: <path> (<N> bytes)»` `` (exact text finalized with format goldens).
- Truncation: cut at the last newline before the cap; marker line:
  `` `… [truncated at <N> est. tokens]` ``.

### 4.5 Tree renderer
Sorted paths, two-space indentation, **plain ASCII** connectors (`+--`, `|  `);
content = included files only. A unicode box-drawing option is post-v1.

### 4.6 Token estimator (spec + calibration policy)
- Input per file: bytes `B`, UTF-8 chars `C`, lines `L`.
- v1 formula: to be fixed during M2 calibration; candidate family
  `est = ceil((C + α·L) / β)` with α, β fitted on the fixture corpus.
- Calibration: run a reference tokenizer (tiktoken, if installable locally) over the
  fixture corpus; record formula, α/β, and mean absolute error here before v0.1.0.
  Until recorded: outputs carry `est.` labels and README makes no numeric claim.

## 5. Output format contracts (grammar level)

- **md** — header, fenced tree, per file: `### <path>` + fenced block with language
  hint from extension.
- **xml** — `<context root="…">` + `<tree>` + `<file path="…">content</file>`.
- **json** — `{"root":…,"tree":[…],"files":[{"path":…,"content":…,"tokens":…}]}`.
- **txt** — minimal: header line, `===== <path> =====`, content.

Escaping (fences in content, XML entities, JSON strings) is delegated to cJSON for
JSON and to a small tested escaper for the others. Final goldens pin the exact bytes.

## 6. Error handling policy

| Condition | Action | Exit |
|---|---|---|
| unknown flag / bad args | usage on stderr | 2 |
| invalid `.cctxpack.json` | stderr with cJSON error offset | 2 |
| `diff` outside git work tree | stderr | 2 |
| `--clipboard` on non-Windows | stderr | 2 |
| git command fails | stderr + git's message | 1 |
| file I/O failure | stderr, fail fast (D-10) | 1 |
| budget infeasible | stderr report | 3 |
| success | data on stdout | 0 |

## 7. Testing strategy

- **L1 unit** — per-module greatest.h suites (glob matrix, estimator goldens, config
  precedence, fit invariants: output ≤ budget; include/exclude inverses; binary
  sniff; path normalization both separators).
- **L2 golden e2e** — CTest script runs the built binary against
  `tests/fixtures/repo` across the flag matrix; byte-compare with `tests/golden/`.
- **L3 invariants** — pack twice, byte-compare (determinism, NFR-07); budget never
  exceeded for random budgets on the fixture corpus (in C, seeded PRNG).
- **L4 platform CI** — matrix: windows-msvc (Ninja), windows-mingw (msys2 action),
  ubuntu gcc + clang, macos clang. Git-dependent tests (ls-files, diff) create a
  temp repo at setup or skip via CTest `SKIP_RETURN_CODE` when git is absent.
- Windows-specific: UTF-8 filename pack test (exercises wide-path open in util).

## 8. Build & release

- One target (`cctxpack`) + tests (added M1). Version single-sourced from
  `project(VERSION …)` via `target_compile_definitions(CTX_VERSION=…)`.
- C standard detection loop lives in `CMakeLists.txt` (D-01), status printed.
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
- Config discovery: walk up to git root; cwd-only fallback outside a work tree.
- `--budget` unset = pack everything; report auto-fires whenever a budget is set.
- Tree: plain ASCII from day one. `diff`: unstaged default + `--staged`.
- `count --format json` stays in v1 (FR-011). License MIT. Name: **cctxpack**.

Remaining open (implementation-time, no design impact):
1. getopt implementation choice: musl-derived vs NetBSD (decide while vendoring, M1).
2. Estimator formula α/β calibration and recorded MAE (M2, §4.6).
