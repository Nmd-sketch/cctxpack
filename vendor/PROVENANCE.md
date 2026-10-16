# Vendor provenance

| Directory | Library | Version | Source | License |
|---|---|---|---|---|
| `cjson/` | cJSON | v1.7.19 (tag) | https://github.com/DaveGamble/cJSON | MIT (see `LICENSE`) |
| `greatest/` | greatest.h | v1.5.0 (tag) | https://github.com/silentbicycle/greatest | CC0-1.0 (see `LICENSE`) |
| `getopt/` | NetBSD getopt/getopt_long | `getopt_long.c,v 1.28`, trunk, retrieved 2026-10-16 | https://github.com/NetBSD/src — `lib/libc/stdlib/getopt_long.c` | BSD-2-Clause (see `LICENSE`; also in file header) |

All upstream files were retrieved 2026-10-16 from raw.githubusercontent.com and are
committed **unmodified**.

## Our additions in `vendor/getopt` (shims, not upstream files)

- `getopt_compat.h` — force-included when compiling `getopt_long.c`; provides the
  NetBSD macros (`__RCSID`, `__UNCONST`, `_DIAGASSERT`, `__weak_alias`) that other
  libcs' headers do not define.
- `compat/namespace.h` — empty stand-in for NetBSD's internal `namespace.h`.
- `msvc-compat/` — stand-ins for headers MSVC lacks (`sys/cdefs.h`, `err.h`,
  `getopt.h`); only added to the include path when compiling with MSVC.

`getopt_long.c` is compiled with `REPLACE_GETOPT` defined so the file provides
`getopt()`, `getopt_long()`, and the `optarg`/`optind`/`opterr`/`optopt`/`optreset`
globals itself, independent of the platform libc.

Rationale for NetBSD over musl (D-03): musl's getopt pair depends on musl-internal
headers (`libc.h`, `syscall.h`) and would require modifying the vendored source;
NetBSD's single-file implementation needs only additive shims.
