#ifndef CTX_FIND_H
#define CTX_FIND_H

#include "pack.h"

/* --find: fixed-string, line-oriented search (grep -F semantics).  Prints
 * "path:line: text" for each matching line to stdout, or to -o FILE.  Honours
 * path arguments, --include and --exclude; skips binary files.  Fixed-string
 * and case-sensitive by design: no regex, so MSVC needs no POSIX <regex.h>.
 *
 * Returns 0 when at least one match was found, 4 when none was (distinct from
 * 1=I/O error, 2=usage, 3=nothing fits a budget), and 1 on I/O error. */
int find_run(const pack_opts *o);

#endif
