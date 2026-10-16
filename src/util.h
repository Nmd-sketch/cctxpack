#ifndef CTX_UTIL_H
#define CTX_UTIL_H

#include <stddef.h>
#include <stdio.h>

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} strbuf;

void strbuf_init(strbuf *sb);
void strbuf_free(strbuf *sb);
int strbuf_append(strbuf *sb, const char *s);
int strbuf_appendn(strbuf *sb, const char *s, size_t n);
int strbuf_appendf(strbuf *sb, const char *fmt, ...);
char *str_dup(const char *s);
void path_normalize(char *p);

char *path_cwd(void);
char *path_abs(const char *path);
char *path_basename_dup(const char *path);
FILE *path_open(const char *path, const char *mode);

#endif
