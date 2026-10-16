# cctxpack

[![CI](https://github.com/Nmd-sketch/cctxpack/actions/workflows/ci.yml/badge.svg)](https://github.com/Nmd-sketch/cctxpack/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Release](https://img.shields.io/github/v/release/Nmd-sketch/cctxpack)](https://github.com/Nmd-sketch/cctxpack/releases/latest)

Pack a repository (or a subset) into a single, token-budgeted, LLM-ready context bundle. Deterministic, offline-first, and built in C.

- **Minimal & fast.** Single static binary with zero runtime network dependencies (vendored cJSON and getopt).
- **Token-budget aware.** `--budget` uses rendered-token estimates so the emitted bundle actually fits the cap.
- **Multi-format.** Emit as Markdown, XML, JSON, or plain text (`--format md|xml|json|txt`).
- **Git-native.** `diff` and `diff --staged` pack only changed files from the work tree.
- **Smart selection.** `--include/--exclude` globs, `--query` (path-weighted keyword ranking), `--sort name|mtime|size`.
- **Deterministic.** Same inputs → identical bytes (byte-wise path ordering, no locale).
- **Cross-platform.** Builds cleanly on Linux, macOS, and Windows (gcc/clang/MSVC).

## Quick start

### Install

Download a prebuilt binary from [releases](https://github.com/Nmd-sketch/cctxpack/releases/latest) or build from source:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
# binary at build/cctxpack (or build/Release/cctxpack.exe on Windows)
```

### Pack the current repo

```sh
cctxpack . -o context.md
```

### Tight token budget

```sh
cctxpack . --budget 8000 --report
```

### Only what changed (git diff)

```sh
cctxpack diff -o review.md
cctxpack diff --staged -o staged.md
```

### Find and pack relevant files first

```sh
cctxpack . --query "auth jwt" --budget 6000
```

### Machine-readable formats

```sh
cctxpack . --format json -o bundle.json
cctxpack . --format xml  -o bundle.xml
cctxpack . --format txt  -o bundle.txt
```

### Count/take inventory

```sh
cctxpack count .
cctxpack count . --format json
cctxpack tree .
```

### Clipboard (Windows)

```sh
cctxpack . --clipboard
```

### Config file (.cctxpack.json)

cctxpack walks up from the cwd (stopping at the git work-tree root) and merges the nearest `.cctxpack.json`. CLI flags override config.

```json
{
  "budget": 8000,
  "format": "md",
  "tree": true,
  "include": ["src/**", "docs/**"],
  "exclude": ["**/*.log", "build/**"],
  "max_file_tokens": 4000
}
```

## How it works

cctxpack discovers tracked/untracked files (respecting `.gitignore`), applies include/exclude + root filters, sorts deterministically, renders each file into the chosen format with per-section estimates, fits inside `--budget` (reserving header/tree/closing overhead), then emits a single bundle. Token estimates are computed on the *rendered* sections (with `first=0`), so budget math matches the final output.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | OK |
| 1 | I/O/OOM/read error |
| 2 | Usage/config error (invalid JSON, unknown flag, etc.) |
| 3 | Nothing fits the budget |

## Comparison vs repomix

| Aspect | cctxpack | repomix |
|---|---|---|
| Runtime | C (single native binary) | Node.js/TypeScript |
| Dependencies | Vendored only (offline) | Node deps (npm) |
| Token counting | heuristic estimate (`ceil((10C+35L)/38)`, calibrated; **0.61 %** from tiktoken on this repo) | exact BPE (tiktoken `cl100k_base`) |
| Formats | md, xml, json, txt | md (primary), xml/json/txt variants |
| Git-aware | `diff`, `diff --staged` (NUL-split, worktree-aware) | No native diff mode |
| Selection | Path-weighted `--query` + globs + sort (mtime/size/name) | Globs/ignore + some ordering |
| Determinism | Byte-stable across runs (no locale) | Mostly stable; depends on deps/formatting |
| Footprint | 245 KB binary, 8.2 ms startup | Node runtime + npm dep tree |
| Use case | CLI-first, air-gapped, scriptable, reproducible | Polished TS UX |

Honest trade-off: repomix's token counts are **exact** (tiktoken) while ours are
a calibrated heuristic. We buy speed and zero dependencies with ~0.6 % typical
error on ordinary source; the error grows on dense vendored code (see §4.6 of
the design doc).

## Benchmarks

All numbers below were measured on this repository with both tools packing the
**same file set**, on Windows 11 / MSVC Release build, cctxpack 0.1.0-dev vs
repomix 1.18.1. Reproduce with the commands in the footnote.

### Identical inputs — `src/**` (21 files, 81,726 content bytes)

| Metric | cctxpack | repomix 1.18.1 | |
|---|---|---|---|
| Files packed | 21 | 21 | same set |
| Tokens | **24,350** (heuristic estimate) | **24,500** (tiktoken `cl100k_base`) | est. within **0.61 %** of real BPE |
| Output size | 82,682 B | 84,307 B | |
| Wall time | **177 ms** | **~2,950 ms** | **~17× faster** |
| Startup only (`--version`) | 8.2 ms | — | |

### Full repository (each tool's *default* file selection)

The two tools disagree on what to include, so these are not a like-for-like
comparison — shown to document the difference:

| | cctxpack | repomix |
|---|---|---|
| Files | 62 | 47 |
| Tokens | 116,351 (est.) | 64,059 (tiktoken) |
| Output size | 402,762 B | 218,911 B |
| Wall time | 203 ms | ~3,000 ms |

cctxpack's larger total is mostly vendored code it respects as source:
`vendor/greatest/greatest.h` alone is 68,870 bytes / 19,290 est. tokens, and the
token estimator is known to over-count dense vendored text (§4.6 of the design
doc). repomix's default patterns exclude it. Use `--exclude` / `--include` to
match whichever corpus you want.

### Where the time goes

Of the 177 ms for `src/**`, almost none is file I/O: `cctxpack --version`
(startup only) is 8.2 ms and `cctxpack tree .` alone is 167 ms. The fixed cost
is **three `git` subprocess spawns** per run — `git rev-parse --show-toplevel`
for config discovery, `git rev-parse --is-inside-work-tree`, and
`git ls-files -c -o --exclude-standard -z` — each paying Windows process
creation plus `git.exe` load. Packing the 41 extra files on top of that costs
~36 ms.

*Caveat:* single-run-per-process timing on Windows includes PowerShell and
`npx`/Node startup overhead; `npx` alone adds a few hundred ms to the repomix
figure. The gap is large enough that this does not change the ranking, but the
absolute repomix number is slightly pessimistic.

```sh
# cctxpack
cctxpack count . --include "src/**"          # files, bytes, lines, est. tokens
cctxpack . --include "src/**" -o out.md      # wall time via Measure-Command / time
# repomix
npx -y repomix@latest . --include "src/**" --style markdown -o out.txt
```

## License

MIT License. See [LICENSE](LICENSE) for details.

Copyright (c) 2026 Nmd-Sketch.
