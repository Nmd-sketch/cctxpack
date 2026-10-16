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
| `src/main.c` | command dispatch, version, `config_apply` | 100 | 30 | 41 | e2e smoke |
| `src/cli.c` | getopt wrapper, command-aware parsing, flag validation, path-operand resolution, `--help` text | 500 | 373 | 433 | `test_cli.c` |
| `src/cmd.c` | `count` / `tree` / `diff` command bodies | 220 | – | 182 | `test_cli.c` |
| `src/config.c` | `.cctxpack.json` discovery + load (cJSON), precedence merge | 340 | – | 290 | `test_config.c` |
| `src/files.c` | `git ls-files` runner, fallback walker, git top/diff runners, strlist, sort modes | 520 | 320 | 448 | `test_files.c` |
| `src/glob.c` | glob matcher + include/exclude/root filters | 200 | 157 | 157 | `test_files.c` |
| `src/read.c` | open/read, binary sniff, size caps, truncation | 175 | 149 | 149 | `test_read.c` |
| `src/tokens.c` | calibrated heuristic estimator | 120 | 30 | 30 | `test_tokens.c` |
| `src/pack.c` | ordering (`key`/`key2`/`path`), `pack_score`, sink, budget fit, report assembly | 460 | 239 | 392 | `test_pack.c` |
| `src/format.c` | md/xml/json/txt emitters, tree renderer, escapers | 450 | 170 | 388 | `test_format.c` |
| `src/util.c` | strbuf, `str_dup`, lexical `/`-normalization, UTF-8→wide path open, UTF-8→UTF-16LE | 360 | 242 | 311 | `test_util.c` |
| **total (`.c`)** | | | 1,710 | **2,821** | |
| **total (`.c`+`.h`)** | | **≤ 3,500 (NFR-09)** | 1,857 | **3,075** | |

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
