#ifndef CTX_FORMAT_H
#define CTX_FORMAT_H

#include "util.h"

#include <stddef.h>

void format_header(strbuf *b, const char *root_display);
void format_tree(strbuf *b, char *const *paths, size_t n);
void format_section(strbuf *b, const char *path, const char *content,
    size_t len);
void format_binary(strbuf *b, const char *path, size_t size);

#endif
