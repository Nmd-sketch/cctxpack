#include "glob.h"

#include <stdlib.h>
#include <string.h>

static int match_here(const char *p, const char *g);

static int match_star(const char *p, const char *g)
{
    const char *q = p;
    do {
        if (match_here(q, g))
            return 1;
    } while (*q && *q++ != '/');
    return 0;
}

static int match_class(const char *p, const char *g)
{
    const char *g0 = g;
    g++;
    int negate = 0;
    if (*g == '!' || *g == '^') {
        negate = 1;
        g++;
    }
    int match = 0;
    int first = 1;
    for (; *g && (*g != ']' || first); first = 0, g++) {
        if (g[1] == '-' && g[2] && g[2] != ']') {
            unsigned char lo = (unsigned char)g[0];
            unsigned char hi = (unsigned char)g[2];
            unsigned char c = (unsigned char)*p;
            if (lo <= hi ? (c >= lo && c <= hi) : (c >= hi && c <= lo))
                match = 1;
            g += 2;
        } else if (*g == (unsigned char)*p) {
            match = 1;
        }
    }
    if (*g != ']')
        return *p == '[' && match_here(p + 1, g0 + 1);
    if (!*p || *p == '/' || match == negate)
        return 0;
    return match_here(p + 1, g + 1);
}

static int match_here(const char *p, const char *g)
{
    if (!*g)
        return !*p;
    if (g[0] == '*') {
        if (g[1] == '*') {
            const char *rest = g + 2;
            if (*rest == '/') {
                rest++;
                for (const char *q = p;;) {
                    if (match_here(q, rest))
                        return 1;
                    while (*q && *q != '/')
                        q++;
                    if (!*q)
                        return 0;
                    q++;
                }
            }
            for (const char *q = p;; q++) {
                if (match_here(q, rest))
                    return 1;
                if (!*q)
                    break;
            }
            return 0;
        }
        return match_star(p, g + 1);
    }
    if (!*p)
        return 0;
    if (*g == '?')
        return *p != '/' && match_here(p + 1, g + 1);
    if (*g == '[')
        return match_class(p, g);
    if (*g == *p)
        return match_here(p + 1, g + 1);
    return 0;
}

static int has_slash(const char *s)
{
    return strchr(s, '/') != NULL;
}

int glob_match(const char *path, const char *pattern)
{
    if (has_slash(pattern))
        return match_here(path, pattern);
    if (match_here(path, pattern))
        return 1;
    for (const char *p = path; *p; p++) {
        if (*p == '/' && match_here(p + 1, pattern))
            return 1;
    }
    return 0;
}

void glob_filter(strlist *paths, const strlist *include, const strlist *exclude)
{
    size_t out = 0;
    for (size_t i = 0; i < paths->n; i++) {
        const char *p = paths->v[i];
        int keep = 1;
        if (include && include->n > 0) {
            keep = 0;
            for (size_t j = 0; j < include->n && !keep; j++) {
                if (glob_match(p, include->v[j]))
                    keep = 1;
            }
        }
        if (keep && exclude) {
            for (size_t j = 0; j < exclude->n && keep; j++) {
                if (glob_match(p, exclude->v[j]))
                    keep = 0;
            }
        }
        if (keep) {
            paths->v[out++] = paths->v[i];
        } else {
            free(paths->v[i]);
        }
    }
    paths->n = out;
}

void root_filter(strlist *paths, const strlist *roots)
{
    if (!roots || roots->n == 0)
        return;
    size_t out = 0;
    for (size_t i = 0; i < paths->n; i++) {
        char *p = paths->v[i];
        size_t plen = strlen(p);
        int keep = 0;
        for (size_t j = 0; j < roots->n && !keep; j++) {
            const char *r = roots->v[j];
            size_t rlen = strlen(r);
            if (rlen <= plen && memcmp(p, r, rlen) == 0 &&
                (rlen == plen || p[rlen] == '/'))
                keep = 1;
        }
        if (keep) {
            paths->v[out++] = p;
        } else {
            free(p);
        }
    }
    paths->n = out;
}
