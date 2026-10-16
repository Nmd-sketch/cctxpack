#ifndef CTX_TOKENS_H
#define CTX_TOKENS_H

#include <stddef.h>

size_t tokens_estimate(const char *s, size_t len);
size_t tokens_estimate_parts(size_t chars, size_t lines);

#endif
