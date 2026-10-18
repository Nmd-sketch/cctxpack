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

Download a prebuilt binary from [releases](https://github.com/Nmd-sketch/cctxpack/releases/latest), or build from source:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
# binary at build/cctxpack (or build/Release/cctxpack.exe on Windows)
```

<details>
<summary>Linux, macOS, Windows install</summary>

**Linux (static, no runtime deps):**

```sh
VERSION=0.1.0
curl -fsSLO "https://github.com/Nmd-sketch/cctxpack/releases/download/v${VERSION}/cctxpack-linux-x86_64.tar.gz"
sha256sum -c SHA256SUMS 2>/dev/null || sha256sum cctxpack-linux-x86_64.tar.gz
tar xzf cctxpack-linux-x86_64.tar.gz
sudo install -m 0755 pkg/cctxpack /usr/local/bin/cctxpack
```

**macOS (universal — Apple Silicon and Intel):**

```sh
VERSION=0.1.0
curl -fsSLO "https://github.com/Nmd-sketch/cctxpack/releases/download/v${VERSION}/cctxpack-macos-universal.tar.gz"
tar xzf cctxpack-macos-universal.tar.gz
sudo install -m 0755 pkg/cctxpack /usr/local/bin/cctxpack
```

**Windows (PowerShell):**

```powershell
$VERSION = "0.1.0"
Invoke-WebRequest "https://github.com/Nmd-sketch/cctxpack/releases/download/v$VERSION/cctxpack-windows-x64.zip" -OutFile cctxpack.zip
Expand-Archive cctxpack.zip -DestinationPath .
.\pkg\cctxpack.exe --version
```

A second Windows build is available as `cctxpack-windows-x64-mingw.tar.gz`
(MinGW, also static).

Every release ships a `SHA256SUMS` file. Verify with `sha256sum -c SHA256SUMS`
(Linux/macOS) or `Get-FileHash` (Windows).

</details>

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
cctxpack diff -o review.md          # staged + unstaged + untracked
cctxpack diff --staged -o staged.md # index only
cctxpack diff --patch -o patch.md   # the unified diff itself
```

`diff` reports **everything not yet committed** — staged, unstaged *and*
untracked files. Deleted paths are skipped, since there is nothing to pack.

`--patch` emits the unified diff rather than whole files. That matters when the
model has no earlier copy of the file to compare against (a fresh session, or
after context compaction): full files plus a remembered old version shows what
changed, but full files alone do not. It is also dramatically cheaper — the
patch is a few hundred tokens where the files were tens of thousands.

```sh
cctxpack diff --patch --format json   # machine-readable, still valid JSON
```

Untracked files have no diff representation, so they appear only in plain
`cctxpack diff`, never in `--patch`.

### Dropping context without touching git (`.cctxpackignore`)

```gitignore
# .cctxpackignore — one glob per line, '#' comments, blank lines ignored
vendor/*
*.snap
docs/internal/**
```

Layered on top of `.gitignore`, not a replacement: files stay tracked by git,
they just stop being packed. A missing file is not an error.

### Find and pack relevant files first

```sh
cctxpack . --query "auth jwt" --budget 6000
```

### Redacting credentials before you send a repo to a model

**Opt-in.** Without `--redact` you get exactly the bytes on disk — fidelity is the
whole point of this tool, and quietly rewriting content would break it.

```sh
cctxpack . --redact -o safe.md
# cctxpack: redacted 2 credential(s)
```

```diff
- AWS_KEY = "AKIAIOSFODNN7EXAMPLE"
- db_password = "s3cr3t-prod-value"
+ AWS_KEY = "[REDACTED:aws-access-key-id]"
+ db_password = "[REDACTED:password]"
```

It replaces whole PEM private key blocks, vendor token shapes (`AKIA…`, `ghp_`,
`github_pat_`, `AIza…`, `xox…`, `sk_live_`, JWTs), and values assigned to
unambiguously secret names (`password`, `secret`, `api_key`, `access_token`,
`client_secret`, …) of at least 6 bytes.

Two properties worth knowing:

- **The line count is preserved exactly.** A multi-line key block collapses to a
  marker plus blank lines, so line numbers you were given still point where they
  did. Only the secret span is rewritten, so the statement around it still parses.
- **The count is reported**, in the manifest and on stderr, so an agent can tell
  a scrubbed file from an empty one.

**What it deliberately does not catch.** There is no entropy heuristic, and that
is the important limitation: length-and-alphabet scoring mislabels SHA digests,
base64 assets, lockfile integrity hashes and minified bundles far more often than
it catches a real key. Placeholders (`your-…`, `${VAR}`, `<…>`, `changeme`) and
derived values (`password_hash`) are left alone, as are secrets that live only in
environment variables.

**This is not a security guarantee.** It reduces accidental disclosure of
committed credentials; it does not replace a secret scanner, and it will miss
credentials in shapes it does not know. See the man page's REDACTION section for
the full list.

### Machine-readable formats

```sh
cctxpack . --format json -o bundle.json
cctxpack . --format xml  -o bundle.xml
cctxpack . --format txt  -o bundle.txt
```

Every bundle starts with a **provenance manifest** — commit SHA, file count,
content bytes, estimated tokens, and a digest identifying the exact file set:

```markdown
<!-- cctxpack-manifest commit=8125733d18ee files=9 bytes=4191 tokens=1242 digest=9d2ae02d631a65a0 -->
```

That matters most for agents: the consumer can say which snapshot it received
and whether two bundles hold the same files. JSON gets a `"manifest"` object,
XML attributes on `<context>`, txt a `manifest:` line. There is deliberately
**no timestamp** — two runs over an unchanged tree are byte-identical.

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
| Token counting | script-aware heuristic, 8 per-language rates (**+10.8% aggregate / up to −26% per file**) | exact BPE (tiktoken `cl100k_base`) |
| Formats | md, xml, json, txt | md (primary), xml/json/txt variants |
| Git-aware | `diff`, `diff --staged` (NUL-split, worktree-aware) | No native diff mode |
| Selection | Path-weighted `--query` + globs + sort (mtime/size/name) | Globs/ignore + some ordering |
| Determinism | Byte-stable across runs (no locale) | Mostly stable; depends on deps/formatting |
| Footprint | 246 KB binary, 8.2 ms startup | Node runtime + npm dep tree |
| Use case | CLI-first, air-gapped, scriptable, reproducible | Polished TS UX |

Honest trade-off: repomix's token counts are **exact** (tiktoken) while ours are
a calibrated script-aware heuristic. We buy a 246 KB dependency-free binary for
that, and we bias high — over-estimating wastes a little budget, under-estimating
fails the request.

### How accurate is `--budget`?

Counts are **estimates** (`est.`), labelled as such everywhere. There are **two
different numbers**, and conflating them is how the previous version of this
README ended up claiming "+0.61% accurate", which was wrong.

**Aggregate — "how many tokens is this repo?"**

| Corpus | Files | Aggregate | Files under-estimated |
|---|---:|---:|---:|
| a C++ repo | 99 | **+24.6%** | 1.0% |
| this repository (C) | 64 | **+10.4%** | 26.6% |
| a Markdown-heavy repo | 336 | **+30.2%** | 2.7% |

**Per-file — "how many tokens is this file?"**

| Corpus | Median | p90 | Worst | Under |
|---|---:|---:|---:|---:|
| a C++ repo | +32.2% | +52.0% | **−2.5%** | 1.0% |
| this repository (C) | +5.6% | +36.8% | **−26.1%** | 26.6% |
| a Markdown-heavy repo | +31.4% | +46.0% | **−17.3%** | 2.7% |

Reproduce all of it with:

```sh
python tools/benchmark.py "<repo-a>" "<repo-b>" "<repo-c>"
```

The harness shells out to the **real binary** rather than reimplementing the
estimator in Python, and excludes binary files (cctxpack does not estimate them).
Results land in `tools/benchmark-results.md`.

**Prose over-estimates more than code.** The Markdown corpus sits ~6 points
higher than C++ at the median — English prose is full of short common words
that BPE merges aggressively, so a per-character rate runs high. CJK text
over-estimates least (+22% median on the 9 CJK files sampled) but has the
thinnest evidence behind it.

Why aggregate is so much better than per-file: per-file errors are close to
zero-mean, so they **cancel** across a repo — measured summed error is ~0.1% of
the corpus total, against a 2.1% per-file spread. A budget set from the
aggregate is reliable; a per-file number in `count` output is not, and is
labelled as an estimate for that reason.

The per-file floor is not a modelling deficiency. Two text chunks with
**identical** character-class, line-length, word-length and whitespace
statistics differ by a median of ~8% and ~25% at p90 in true token count,
because BPE merges common subwords — `the` is 1 token, `zqjx` is 3, at identical
character rates. Recovering that needs the vocabulary, which is a 2.3 MB table
we do not ship (see the design doc). Distinct-word-count features were measured
and do nothing; the missing information is vocabulary *identity*, which no
single-pass byte histogram can carry.

Two more things worth knowing:

- **We deliberately over-estimate** on non-ASCII content. The previous model
  charged every character the ASCII rate, which put CJK text **48% under** at the
  median and **86% under** at worst — so `--budget 100000` could really emit
  186,000 tokens and overflow the context window.
- **Binary files are not estimated.** A NUL byte in the first 8 KB marks a file
  binary; cctxpack emits a placeholder carrying its path and byte size instead.

Reproduce with `python tools/calibrate_tokens.py --compare` and
`tools/benchmark.py`; full record in
[design §4.6](docs/design-document.md).

## Benchmarks

All numbers below were measured on this repository with both tools packing the
**same file set**, on Windows 11 / MSVC Release build, cctxpack 0.1.0-dev vs
repomix 1.18.1. Reproduce with the commands in the footnote.

### Identical inputs — `src/**` (21 files, 86,000 content bytes)

| Metric | cctxpack | repomix 1.18.1 | |
|---|---|---|---|
| Files packed | 21 | 21 | same set |
| Tokens | **25,650** (heuristic estimate) | **26,239** (tiktoken `cl100k_base`) | ours is −2.2% here; aggregate bias is +11.6% |
| Output size | 86,958 B | 88,583 B | |
| Wall time | **183 ms** | **~2,550 ms** | **~14× faster** |
| Startup only (`--version`) | 8.2 ms | — | |

### Full repository (each tool's *default* file selection)

The two tools disagree on what to include, so these are not a like-for-like
comparison — shown to document the difference:

| | cctxpack | repomix |
|---|---|---|
| Files | 62 | 47 |
| Tokens | 122,152 (est.) | 70,068 (tiktoken) |
| Output size | 421,172 B | 237,321 B |
| Wall time | 205 ms | ~2,550 ms |

cctxpack's larger total is mostly vendored code it respects as source:
`vendor/greatest/greatest.h` alone is 68,870 bytes / 19,348 est. tokens, and the
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
