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
review threshold (P1); the M2 column records the measurement at the M2 code
review (2026-10-16), where caps were revised to measured value + ~15% headroom.

| Module | Responsibility | LOC cap | LOC (M2) | Unit tests |
|---|---|---|---|---|
| `src/main.c` | command dispatch, version | 100 | 30 | e2e smoke |
| `src/cli.c` | getopt wrapper, flag validation, path-operand resolution, `--help` text | 430 | 373 | `test_cli.c` |
| `src/config.c` | `.cctxpack.json` load (cJSON), precedence merge | 200 | – (M3) | `test_config.c` |
| `src/files.c` | `git ls-files` runner, fallback walker, strlist, sort | 350 | 320 | `test_files.c` |
| `src/glob.c` | glob matcher + include/exclude/root filters | 200 | 157 | `test_files.c` |
| `src/read.c` | open/read, binary sniff, size caps, truncation | 175 | 149 | `test_read.c` |
| `src/tokens.c` | calibrated heuristic estimator | 120 | 30 | `test_tokens.c` |
| `src/pack.c` | ordering, budget fit, report assembly | 250 | 239 | `test_pack.c` |
| `src/format.c` | md/xml/json/txt emitters, tree renderer | 350 | 170 | `test_format.c` |
| `src/util.c` | strbuf, `str_dup`, lexical `/`-normalization, UTF-8→wide path open | 280 | 242 | `test_util.c` |
| **total** | | **≤ 2,000** | 1,710 (`.c`) / 1,857 (`.c`+`.h`) | |

The 2,000 ceiling counts first-party production lines only. At 1,857 the
planned M3 `config.c` must stay near its 140-line estimate to fit; if it does
not, the ceiling is revisited deliberately rather than by drift.

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
- Input per file: UTF-8 code points `C` and newlines `L`. Bytes/`B` is not used
  (byte length is a poor proxy across UTF-8 and ASCII).
- Formula: `est = ceil((C + α·L) / β)`.
- **Calibrated constants (2026-10-16): α = 3.5, β = 3.8.** Because α and β are
  exact rationals (7/2 and 19/5), the ceiling is evaluated in integer arithmetic:
  `est = ceil((10·C + 35·L) / 38)`, which is bit-identical on every platform (P4)
  and is what `src/tokens.c` implements.
- **Corpus**: the repository's 41 first-party text files (source, tests, docs,
  tools, root; 120,685 bytes). Vendored third-party code is excluded: its token
  density is unrepresentative and it dominated the token mass.
- **Reference tokenizer**: tiktoken `cl100k_base` (GPT-4/3.5), cross-checked on
  `o200k_base` (GPT-4o).
- **Fit**: unweighted least squares `T = p·C + q·L` gives α = 3.403, β = 3.856
  (cl100k) and α = 3.615, β = 3.865 (o200k). Rounded to α = 3.5, β = 3.8; β is
  rounded *down*, biasing the estimate ~2% high (the safe direction for a budget).
- **Measured error at α = 3.5, β = 3.8**:
  - cl100k: MAE **41.9 tokens/file**, median rel. err 5.9 %, p90 25.0 %,
    11/41 files under-estimated, worst single-file under-count 5.94 %
    (`src/util.c`), corpus aggregate **+2.07 %**.
  - o200k: MAE 40.9, median 5.8 %, p90 25.0 %, worst under-count 6.58 %,
    aggregate +1.58 %.
- **Reproduce**: `python tools/calibrate_tokens.py --all-encodings
  --exclude-prefix vendor/ --alpha 3.5 --beta 3.8 --show-worst`
- **Limitations**: this is a heuristic, not a tokenizer. Per-file error is
  typically single-digit percent but reaches ~25 % on short or unusually dense
  files; atypical (e.g. dense vendored) text is over-counted. Outputs therefore
  keep the `est.` label and the README makes no precise claim. An exact BPE
  tokenizer is the post-v1 option (D-05).

The fitting invariant (the bundle's own estimate never exceeds the sum of the
part estimates counted while fitting) holds because `est` is the ceiling of an
additive linear form, and is checked by `test_tokens.c`.

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

Remaining open (implementation-time, no design impact):
1. None for M2.
