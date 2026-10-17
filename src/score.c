/* Lexical path scoring for --query.
 *
 * Extracted from pack.c as a separate, independently testable concern, in the
 * same way glob.c was split out of files.c: pattern matching and byte
 * comparison are not budget fitting.
 *
 * This is lexical overlap, NOT semantic relevance.  It knows nothing about what
 * the words mean, only that they appear in the path.
 */

#include "score.h"

#include <string.h>

static int lower_ascii(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static size_t count_ci(const char *hay, size_t hlen, const char *term,
    size_t tlen)
{
    size_t n = 0;
    if (tlen == 0 || hlen < tlen)
        return 0;
    for (size_t i = 0; i + tlen <= hlen;) {
        size_t j = 0;
        while (j < tlen
            && lower_ascii((unsigned char)hay[i + j])
                == lower_ascii((unsigned char)term[j]))
            j++;
        if (j == tlen) {
            n++;
            i += tlen;
        } else {
            i++;
        }
    }
    return n;
}

size_t pack_score(const char *path, const char *query)
{
    size_t plen = strlen(path);
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    size_t blen = plen - (size_t)(base - path);
    size_t score = 0;
    const char *q = query;
    while (*q) {
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
            q++;
        const char *t = q;
        while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r')
            q++;
        size_t tlen = (size_t)(q - t);
        if (tlen == 0)
            continue;
        score += count_ci(path, plen, t, tlen);
        score += count_ci(base, blen, t, tlen);
    }
    return score;
}