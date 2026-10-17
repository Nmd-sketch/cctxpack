#ifndef CTX_TOKENS_H
#define CTX_TOKENS_H

#include <stddef.h>

size_t tokens_estimate(const char *s, size_t len);
/* ASCII-only shortcut (all chars at the ASCII rate): kept for the additivity
 * invariant test and for callers that already know the text is ASCII. */
size_t tokens_estimate_ascii(size_t chars, size_t lines);

#endif