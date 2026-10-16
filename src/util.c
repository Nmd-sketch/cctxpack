#include "util.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#endif

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

int strbuf_appendn(strbuf *sb, const char *s, size_t n)
{
    if (strbuf_reserve(sb, n) != 0)
        return -1;
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
    return 0;
}

int strbuf_append(strbuf *sb, const char *s)
{
    return strbuf_appendn(sb, s, strlen(s));
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

char *str_dup(const char *s)
{
    size_t n = strlen(s);
    char *d = malloc(n + 1);
    if (!d)
        return NULL;
    memcpy(d, s, n + 1);
    return d;
}

char *path_cwd(void)
{
#ifdef _WIN32
    return _getcwd(NULL, 0);
#else
    return getcwd(NULL, 0);
#endif
}

char *path_abs(const char *path)
{
    char *out = NULL;
#ifdef _WIN32
    out = _fullpath(NULL, path, 0);
#else
    if (path[0] == '/') {
        out = str_dup(path);
    } else {
        char *cwd = path_cwd();
        if (cwd) {
            size_t n = strlen(cwd) + 1 + strlen(path) + 1;
            out = malloc(n);
            if (out)
                snprintf(out, n, "%s/%s", cwd, path);
            free(cwd);
        }
    }
#endif
    if (!out)
        return NULL;
    path_normalize(out);
    return out;
}

char *path_basename_dup(const char *path)
{
    const char *s = path;
    size_t len = strlen(path);
    while (len > 1 && (s[len - 1] == '/' || s[len - 1] == '\\'))
        len--;
    const char *base = s;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '/' || s[i] == '\\')
            base = s + i + 1;
    }
    size_t blen = len - (size_t)(base - s);
    char *out = malloc(blen + 1);
    if (!out)
        return NULL;
    memcpy(out, base, blen);
    out[blen] = '\0';
    if (blen == 0) {
        out[0] = '/';
        out[1] = '\0';
    }
    return out;
}

FILE *path_open(const char *path, const char *mode)
{
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0)
        return NULL;
    wchar_t *wpath = malloc((size_t)wlen * sizeof(wchar_t));
    if (!wpath)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);
    wchar_t wmode[8];
    size_t m = 0;
    while (mode[m] && m < 7) {
        wmode[m] = (wchar_t)mode[m];
        m++;
    }
    wmode[m] = L'\0';
    FILE *f = _wfopen(wpath, wmode);
    free(wpath);
    return f;
#else
    return fopen(path, mode);
#endif
}
