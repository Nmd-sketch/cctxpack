#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void strbuf_init(strbuf *sb)
{
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void strbuf_free(strbuf *sb)
{
    free(sb->data);
    strbuf_init(sb);
}

static int strbuf_reserve(strbuf *sb, size_t extra)
{
    size_t need = sb->len + extra + 1;
    if (need <= sb->cap)
        return 0;
    size_t cap = sb->cap ? sb->cap * 2 : 64;
    while (cap < need)
        cap *= 2;
    char *data = realloc(sb->data, cap);
    if (!data)
        return -1;
    sb->data = data;
    sb->cap = cap;
    return 0;
}

int strbuf_append(strbuf *sb, const char *s)
{
    size_t n = strlen(s);
    if (strbuf_reserve(sb, n) != 0)
        return -1;
    memcpy(sb->data + sb->len, s, n + 1);
    sb->len += n;
    return 0;
}

int strbuf_appendf(strbuf *sb, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        va_end(ap2);
        return -1;
    }
    if (strbuf_reserve(sb, (size_t)n) != 0) {
        va_end(ap2);
        return -1;
    }
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    sb->len += (size_t)n;
    return 0;
}

void path_normalize(char *p)
{
    for (; *p; p++) {
        if (*p == '\\')
            *p = '/';
    }
}
