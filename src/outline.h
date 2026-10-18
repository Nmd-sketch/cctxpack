#ifndef CTX_OUTLINE_H
#define CTX_OUTLINE_H

#include <stddef.h>

/* One line of an outline: the declaration as it appeared, and where. */
typedef struct {
    size_t line;        /* 1-based line number in the original file */
    char *decl;         /* owned, NUL-terminated */
} outline_item;

typedef struct {
    outline_item *v;
    size_t n;
    size_t cap;
} outline_list;

/* Build an outline of `data`, or report that it cannot be done.
 *
 * Returns 0 on success (possibly with n == 0), or -1 when the file could not be
 * parsed confidently, in which case `out` is left empty and the CALLER MUST fall
 * back to the full text.
 *
 * The fallback is the important part.  This is a heuristic scanner, not a
 * parser: macros, K&R definitions, template metaprogramming and minified
 * output all defeat it.  A confident wrong outline is worse than no outline,
 * so anything suspicious is reported as unparseable instead.
 *
 * Supported families (see outline_family):
 *   brace  C, C++, C#, Java, JavaScript, TypeScript, Go, Rust, Swift, Kotlin
 *   indent Python
 *
 * `imports` are emitted first: they say what a file depends on, which is the
 * cheapest structural information there is.
 */
int outline_build(const char *data, size_t len, const char *path,
    outline_list *out);

/* Which scanner a path gets.  Returns 0 for brace, 1 for indent, -1 for
 * "not outline-able" (prose, data, binaries, unknown extensions), which the
 * caller treats as unparseable. */
int outline_family(const char *path);

void outline_free(outline_list *l);

#endif