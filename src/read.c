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

static void truncate_at_token_cap(read_file *r, size_t max_tokens)
{
    if (tokens_estimate(r->data, r->len) <= max_tokens)
        return;
    size_t cut = r->len;
    for (;;) {
        if (tokens_estimate(r->data, cut) <= max_tokens)
            break;
        if (cut == 0)
            break;
        cut--;
        while (cut > 0 && r->data[cut - 1] != '\n')
            cut--;
    }
    r->len = cut;
    char marker[96];
    snprintf(marker, sizeof(marker), "… [truncated at %zu est. tokens]",
        max_tokens);
    append_marker(r, marker);
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
    if (capped) {
        char marker[64];
        snprintf(marker, sizeof(marker), "… [truncated at %zu byte read cap]",
            (size_t)READ_CAP_DEFAULT);
        append_marker(out, marker);
    } else if (max_tokens > 0) {
        truncate_at_token_cap(out, max_tokens);
    }
    return 0;
}

void read_file_free(read_file *r)
{
    free(r->data);
    r->data = NULL;
    r->len = 0;
}
