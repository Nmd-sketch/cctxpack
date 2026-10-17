#ifndef CTX_SCORE_H
#define CTX_SCORE_H

#include <stddef.h>

/* FR-015: ASCII case-insensitive, non-overlapping occurrence count of every
 * whitespace-separated query term in a path.  A hit in the basename counts
 * twice, because that is the part a human searching by name would match. */
size_t pack_score(const char *path, const char *query);

#endif