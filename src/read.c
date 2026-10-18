#include "read.h"

#include "tokens.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#endif

static int file_size_of(const char *path, uint64_t *out)
{
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0)
        return -1;
    wchar_t *wpath = malloc((size_t)wlen * sizeof(wchar_t));
    if (!wpath)
        return -1;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);
    struct _stat64 st;
    int rc = _wstat64(wpath, &st);
    free(wpath);
    if (rc != 0)
        return -1;
    *out = (uint64_t)st.st_size;
    return 0;
#else
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    *out = (uint64_t)st.st_size;
    return 0;
#endif
}

const char *trunc_reason_name(int reason)
{
    switch (reason) {
    case TRUNC_READ_CAP:  return "read-cap";
    case TRUNC_TOKEN_CAP: return "token-cap";
    default:              return "none";
    }
}

static int append_marker(read_file *r, const char *marker)
{
    size_t mlen = strlen(marker);
    size_t extra = (r->len > 0 && r->data[r->len - 1] != '\n') ? 1 : 0;
    char *data = realloc(r->data, r->len + extra + mlen + 2);
    if (!data)
        return -1;
    r->data = data;
    if (extra)
        r->data[r->len++] = '\n';
    memcpy(r->data + r->len, marker, mlen);
    r->len += mlen;
    r->data[r->len++] = '\n';
    r->data[r->len] = '\0';
    r->truncated = 1;
    return 0;
}

/* Largest prefix of `r` whose estimate fits `max_tokens`, rounded down to a
 * line boundary.  Pure: does not touch `r`.
 *
 * tokens_estimate is a non-decreasing sum over the prefix, so the cut point is
 * found by BINARY SEARCH and then rounded down to a line.  Scanning backwards
 * one line at a time - the obvious way to round - re-estimates the entire
 * prefix on every step, which is quadratic: on a 4 MB file with
 * --max-file-tokens 2000 that is ~80,000 passes over 4 MB, and it never
 * finishes. */
static size_t token_cut(const read_file *r, size_t max_tokens)
{
    size_t len = r->len;
    if (tokens_estimate(r->data, len) <= max_tokens)
        return len;
    size_t lo = 0;                   /* empty prefix always fits */
    size_t hi = len;                 /* and `len` is known not to */
    while (lo < hi) {
        size_t mid = lo + (hi - lo + 1) / 2;   /* ceiling, so mid > lo */
        if (tokens_estimate(r->data, mid) <= max_tokens)
            lo = mid;
        else
            hi = mid - 1;
    }
    while (lo > 0 && r->data[lo - 1] != '\n')
        lo--;
    return lo;
}

int read_file_open(const char *path, size_t max_tokens, read_file *out)
{
    memset(out, 0, sizeof(*out));
    if (file_size_of(path, &out->file_size) != 0)
        return -1;
    FILE *f = path_open(path, "rb");
    if (!f)
        return -1;
    size_t cap = READ_SNIFF + 1;
    char *data = malloc(cap);
    if (!data) {
        fclose(f);
        return -1;
    }
    size_t len = fread(data, 1, cap, f);
    if (ferror(f)) {
        free(data);
        fclose(f);
        return -1;
    }
    if (len > 0) {
        size_t scan = len < READ_SNIFF ? len : READ_SNIFF;
        for (size_t i = 0; i < scan; i++) {
            if (data[i] == '\0') {
                out->binary = 1;
                free(data);
                fclose(f);
                return 0;
            }
        }
    }
    int capped = 0;
    if (len == cap) {
        char *nb = realloc(data, READ_CAP_DEFAULT + 1);
        if (!nb) {
            free(data);
            fclose(f);
            return -1;
        }
        data = nb;
        len += fread(data + len, 1, READ_CAP_DEFAULT - len, f);
        if (ferror(f)) {
            free(data);
            fclose(f);
            return -1;
        }
        if (len == READ_CAP_DEFAULT && !feof(f))
            capped = 1;
    }
    fclose(f);
    data[len] = '\0';
    out->data = data;
    out->len = len;
    out->packed = len;

    /* The token cap is applied even when the byte cap already fired.  A 4 MB
     * read is roughly 1.2M estimated tokens, so skipping the cap here - as an
     * `else if` did - silently violated an explicit --max-file-tokens for
     * every file at or above the read cap.
     *
     * Order matters: the token cap cuts first, so if it fired it is the
     * binding constraint and the only one worth reporting.  The byte cap is
     * still what bounds memory, but when the token cap already cut further it
     * explains nothing about the result. */
    int tok_capped = 0;
    char marker[96];
    marker[0] = '\0';
    if (max_tokens > 0) {
        /* Build the marker first and charge it against the cap.  Cutting to
         * exactly `max_tokens` and then appending the marker overshoots it by
         * the marker's own size, which is a small but real violation of the
         * one guarantee this tool makes.  The marker's text is known up front
         * because max_tokens is, so there is no circularity. */
        snprintf(marker, sizeof(marker), "… [truncated at %zu est. tokens]",
            max_tokens);
        size_t cost = tokens_estimate(marker, strlen(marker)) + 2;
        size_t budget = max_tokens > cost ? max_tokens - cost : 0;
        size_t cut = token_cut(out, budget);
        if (cut < out->len) {
            out->len = cut;
            tok_capped = 1;
        }
    }
    if (tok_capped) {
        append_marker(out, marker);
        out->trunc_reason = TRUNC_TOKEN_CAP;
    } else if (capped) {
        snprintf(marker, sizeof(marker), "… [truncated at %zu byte read cap]",
            (size_t)READ_CAP_DEFAULT);
        append_marker(out, marker);
        out->trunc_reason = TRUNC_READ_CAP;
    }
    return 0;
}

void read_file_free(read_file *r)
{
    free(r->data);
    r->data = NULL;
    r->len = 0;
}
