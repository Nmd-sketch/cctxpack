# Technical Requirements Specification — cctxpack

| | |
|---|---|
| **Version** | 0.1.0-draft |
| **Date** | 2026-10-16 |
| **Status** | Draft — for architecture review |
| **Owner** | Nmd-sketch |
| **Companion** | `docs/design-document.md` (HOW) — this document defines WHAT |

## 1. Introduction

### 1.1 Purpose
This document specifies the externally visible behavior of **cctxpack**. Every requirement
here is testable and traceable to at least one automated test. The Design Document
defines the architecture, algorithms, and recorded decision approvals.

### 1.2 Product summary
cctxpack packs a directory, file set, or git repository into a single LLM-ready prompt
bundle, constrained to an optional token budget, as one statically linked binary with
zero runtime dependencies. Positioning versus incumbents:

| | Install weight | Startup | Budget-fit | C implementation |
|---|---|---|---|---|
| Repomix (Node) | heavy (npm tree) | slow | partial | — |
| code2prompt (Rust) | cargo/build | moderate | flag exists | — |
| Gitingest (Python) | pip + runtime | slow | none | — |
| **cctxpack (C)** | **one binary** | **<15 ms** | **first-class + report** | **first credible one** |

### 1.3 Definitions
- **token (est.)** — output of the heuristic estimator (§4, NFR-06); always an estimate.
- **budget** — maximum estimated tokens the emitted bundle may consume.
- **bundle** — the final text written to stdout/file/clipboard.
- **candidate** — a file that survived discovery and filters, before budget fitting.
- **overhead** — estimated cost of bundle header and directory tree.
- **vendor** — third-party source committed in `vendor/`, compiled in, never fetched at build time.

### 1.4 References
- `docs/design-document.md` — architecture, algorithms, decision records (D-IDs), open questions.
- Approved decisions of 2026-10-16: vendored JSON (D-02), git delegation (D-04),
  vendored getopt (D-03), heuristic estimator (D-05), fail-fast I/O (D-10),
  clip.exe clipboard (D-11), name cctxpack, MIT license.

## 2. Overview

### 2.1 Problem
Feeding a codebase or file set to an LLM is manual and error-prone: users underestimate
token counts, paste partial context, or install heavyweight runtimes to automate it.

### 2.2 Target users
1. Agent/LLM power users assembling session context (opencode, Claude Code, ChatGPT).
2. Developers piping a repo into an LLM CLI or web UI.
3. Windows-first users underserved by Node/Python/Rust tooling.

### 2.3 Goals
Real daily use (release download counts), public attention (100 stars / 90 days),
inbound opportunities (recruiters/companies referencing the profile).

### 2.4 Boundaries
- Read-only over inputs; never writes outside an explicit `-o` path.
- No network access at runtime, ever. No telemetry.
- Single process, single thread, no daemon.

## 3. Functional Requirements

### Commands
- **FR-001** (M) `cctxpack [pack] [paths...]` — pack is the default command when the first
  argument is not a known command. With no paths, use the current directory.
- **FR-002** (M) File discovery — inside a git work tree: `git ls-files` (tracked)
  plus untracked-not-ignored files. Outside git: recursive walk skipping `.git/`.
  Directory symlinks are not followed. Results are order-independent of the OS walk.
- **FR-003** (M) Filtering — `--include <glob>` (if present, only matches survive) and
  `--exclude <glob>` (removes matches). Glob syntax: `*`, `?`, `**`, `[...]`; `/` is
  the path separator in patterns regardless of platform.
- **FR-004** (M) Binary detection — a NUL byte in the first 8192 bytes marks the file
  binary; it is replaced by a placeholder line (exact text: design §4.4) carrying
  path and byte size; content is never emitted.
- **FR-005** (M) Truncation — `--max-file-tokens N` truncates any file whose estimate
  exceeds N at a line boundary, followed by a truncation marker. Absent: no per-file cap.
- **FR-006** (M) Token estimation — heuristic only; numeric outputs are suffixed `est.`
  cctxpack makes no exactness claims (see NFR-06 calibration requirement).
- **FR-007** (M) Budget invariant — with `--budget N`, the estimated tokens of the
  emitted bundle (content + header + tree) never exceed N; if no fitting selection
  exists, exit 3 without emitting a bundle. Without `--budget`, no cap applies:
  everything eligible is packed.
- **FR-008** (M) Report — prints to stderr: used/limit (est.), included count,
  truncated count, excluded files with reasons (`over budget`, `binary`).
  Auto-enabled whenever `--budget` is set; `--report` forces it without a budget.
- **FR-009** (M) Formats — `--format md|xml|json|txt`; all four carry the same
  information set (header, tree, per-file path + content). Grammar: design §5.
- **FR-010** (S) Tree — `--tree` / `--no-tree` include/exclude the directory tree in
  the bundle. Default: on for pack, off for diff. Charset: plain ASCII connectors.
- **FR-011** (M) `cctxpack count [paths...]` — table of per-file bytes / lines / est. tokens
  plus a total row; `--format json` emits the same data as JSON.
- **FR-012** (M) `cctxpack tree [path]` — render the directory tree (respecting default
  ignores) to stdout.
- **FR-013** (M) `cctxpack diff [--staged]` — pack only files changed versus HEAD
  (`--staged`: staged changes). Requires git; outside a work tree: exit 2.
- **FR-014** (M) Config — `.cctxpack.json` in the nearest directory at or above cwd
  (bounded by the git root; outside a git work tree, cwd only). Keys: `budget`,
  `format`, `include`, `exclude`, `max_file_tokens`, `tree`. Precedence:
  command-line flags > config file > defaults. Malformed config: exit 2 with the
  JSON error offset.
- **FR-015** (S) Relevance — `--query "text"` re-ranks candidates by keyword overlap
  with paths/filenames (plain term frequency, path-weighted; deterministic tie-break
  by path). Approved-simple: no TF-IDF, no stemming.
- **FR-016** (S) Clipboard — `--clipboard` copies the final bundle to the Windows
  clipboard instead of stdout. Non-Windows builds: exit 2 with a clear message.
- **FR-017** (M) Output — stdout by default, `-o FILE` to a file (overwrites).
  stdout carries data only; all diagnostics and reports go to stderr.
- **FR-018** (M) `--help` (all commands, all flags) and `--version`.
- **FR-019** (M) Sorting — `--sort name|mtime|size`, default `name` (byte-wise on the
  normalized relative path).

### Exit codes
| Code | Meaning |
|---|---|
| 0 | success |
| 1 | runtime error (I/O failure, git failure) |
| 2 | usage error (unknown flag, bad arguments, invalid config, diff outside git) |
| 3 | budget infeasible (FR-007) |

## 4. Non-functional Requirements

- **NFR-01 Portability** — Windows 10/11 x64 (MSVC, MinGW), Linux x86_64/aarch64
  (gcc, clang), macOS arm64 (clang).
- **NFR-02 Toolchain** — newest C standard the compiler supports; floor C17.
  Detection: C23 → C2x → C17 (MSVC: `/std:c23` → `/std:c17`). CMake ≥ 3.21.
  Verified locally 2026-10-16: gcc 16.2 (C23), clang 22.1 (C23), MSVC 19.44 (C17).
- **NFR-03 Performance** — start ≤ 15 ms; pack 1,000 files / 5 MB in ≤ 300 ms on a
  mid-range dev machine. Numbers published only after measurement (M5 benchmark).
- **NFR-04 Size** — release binary ≤ 1 MB stripped.
- **NFR-05 Zero dependencies, offline build** — all deps vendored and committed;
  configure and build perform **no network access** (hard requirement — constrained
  egress environment). No `FetchContent`, no downloads.
- **NFR-06 Estimation honesty** — the estimator is calibrated against a reference
  tokenizer on the fixture corpus before v0.1.0; measured mean absolute error is
  recorded in the Design Document §4.6. Until then, no numeric accuracy claim.
- **NFR-07 Determinism** — identical inputs and flags produce byte-identical bundles
  on the same platform, and identical bundles across platforms for `/`-safe content.
  No locale-, address-, or hash-order-dependent output.
- **NFR-08 Robustness** — never executes packed content; no shell interpolation of
  user paths in spawned commands; pathological inputs (huge files, symlink loops,
  hostile names) terminate with a clean error, not a hang or crash.
- **NFR-09 Code budget** — own source ≤ 1,800 LOC (vendor and tests excluded).
  Exceeding a module budget (design §2.2) triggers a design review, not more code.
- **NFR-10 Documentation** — every flag and command appears in both `--help` and the
  README; requirements changes update this document in the same commit.
- **NFR-11 Licensing** — MIT for our code; vendored libraries must be MIT/BSD/CC0
  with `LICENSE` + provenance recorded in `vendor/`.

## 5. Interfaces

### 5.1 CLI surface
```
cctxpack [pack] [paths...]        pack (default command)
cctxpack count [paths...]
cctxpack tree [path]
cctxpack diff [--staged]

--budget N          token budget (est.)
--format F          md | xml | json | txt        (default md)
--include GLOB      repeatable
--exclude GLOB      repeatable
--max-file-tokens N per-file truncation cap
--sort S            name | mtime | size          (default name)
--query TEXT        relevance re-rank
--tree / --no-tree  bundle includes the directory tree (default: on)
--report            stderr budget report
-o FILE             write bundle to FILE
--clipboard         copy bundle to Windows clipboard
--help, --version
```

### 5.2 Config file (`.cctxpack.json`)
```json
{
  "budget": 8000,
  "format": "md",
  "include": ["src/**", "docs/**"],
  "exclude": ["**/*.min.js"],
  "max_file_tokens": 4000,
  "tree": true
}
```

### 5.3 Bundle contract (all formats)
1. a header line identifying the root and generation metadata (deterministic fields
   only — no timestamps in v1),
2. the directory tree of included files (unless `--no-tree`),
3. per file: the relative path (normalized `/`) and full or truncated content,
4. binary files: placeholder line only.
Exact grammar per format: Design Document §5.

## 6. Constraints
- **C-01** Offline build only (NFR-05); vendored deps committed in `vendor/`.
- **C-02** Cross-platform binaries produced by CI, not locally (dev machine is
  Windows-only with constrained egress).
- **C-03** The working directive applies: least amount of code; any clever solution
  requires recorded approval in the Design Document §3.
- **C-04** No telemetry, no runtime network, no admin rights required.

## 7. Acceptance criteria — v0.1.0
1. CI green on all four platforms (MSVC, MinGW, Linux gcc/clang, macOS clang).
2. Every (M) requirement has a passing automated test; golden bundles byte-stable.
3. LOC within NFR-09 budget; module budgets in Design §2.2 respected.
4. NFR-06 calibration measured and recorded.
5. README + GIF + benchmark numbers published; release binaries attached to a tag.

## 8. Out of scope (v1)
MCP server, TUI, template systems, remote-URL packing, secret redaction, exact BPE
tokenizer (approval-gated post-v1), non-UTF-8 transcoding, watch/incremental modes,
plugins, shell completions (post-v1, low cost).

## 9. Traceability (FR → test)
| Requirement | Test locus |
|---|---|
| FR-001/017/018 | `tests/test_cli.c` + golden e2e |
| FR-002 | `tests/test_files.c` (+ git fixture) |
| FR-003 | `tests/test_files.c` (glob matrix) |
| FR-004/005 | `tests/test_read.c` |
| FR-006 | `tests/test_tokens.c` (golden corpus) |
| FR-007/008 | `tests/test_pack.c` (invariants) |
| FR-009 | golden e2e, all four formats |
| FR-010/012 | `tests/test_format.c` |
| FR-011 | `tests/test_cli.c` + JSON golden |
| FR-013 | git-dependent e2e (skip when git absent) |
| FR-014 | `tests/test_config.c` |
| FR-015 | `tests/test_pack.c` |
| FR-016 | Windows-only test |
| FR-019 | `tests/test_files.c` |
