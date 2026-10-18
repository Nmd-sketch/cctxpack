#ifndef CTX_REDACT_H
#define CTX_REDACT_H

#include "util.h"

#include <stddef.h>

/* Replace credential-shaped substrings in `data` with `[REDACTED:<kind>]`
 * markers, appending to `out`.  Returns the number of replacements; `out` is
 * still appended to when the result is 0 (so callers can use it
 * unconditionally).
 *
 * Opt-in: nothing is rewritten unless the caller asks for it.  This tool's
 * whole promise is fidelity, and silently altering bytes would break it.
 *
 * Two invariants:
 *   - The number of newlines is preserved exactly, so line numbers a consumer
 *     was given still point where they did before.  A multi-line private key
 *     collapses to one marker line plus the right number of blank ones.
 *   - Only the secret span is replaced.  `password = "[REDACTED:password]"`
 *     keeps the statement intact; the surrounding code is untouched.
 *
 * Not a scanner.  It matches high-confidence credential shapes (see
 * redact.c) and deliberately has no entropy heuristic, because entropy
 * mislabels hashes, base64 assets and minified bundles far more often than it
 * catches anything.
 */
size_t redact_apply(const char *data, size_t len, strbuf *out);

#endif