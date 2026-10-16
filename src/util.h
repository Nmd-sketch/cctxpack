#ifndef CTX_UTIL_H
#define CTX_UTIL_H

#include <stddef.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} strbuf;

void strbuf_init(strbuf *sb);
void strbuf_free(strbuf *sb);
int strbuf_append(strbuf *sb, const char *s);
int strbuf_appendf(strbuf *sb, const char *fmt, ...);
void path_normalize(char *p);

#endif
