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

/* FNV-1a 64.  Seed with FNV1A64_INIT, fold in each chunk.  Used for the bundle
 * digest, which identifies the exact file set and its order - it is not a
 * content-integrity hash. */
#define FNV1A64_INIT 14695981039346656037ULL
unsigned long long fnv1a64(unsigned long long h, const void *data, size_t n);

/* Encode UTF-8 as UTF-16LE with a byte-order mark, for the Windows clipboard.
 * Returns a malloc'd buffer and its byte length (terminator excluded).
 * Malformed UTF-8 bytes become U+FFFD. */
char *utf8_to_utf16le(const char *s, size_t len, size_t *out_len);

char *path_cwd(void);
char *path_abs(const char *path);
char *path_basename_dup(const char *path);
FILE *path_open(const char *path, const char *mode);

#endif
