#include "cmd.h"

#include "cJSON.h"
#include "format.h"
#include "glob.h"
#include "read.h"
#include "outline.h"
#include "tokens.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void join_path(char *buf, size_t cap, const char *root, const char *rel)
{
    snprintf(buf, cap, "%s/%s", root, rel);
}

static int discover(const pack_opts *o, strlist *paths)
{
    if (files_discover(o->root, o->fake_ls, paths, NULL) != 0)
        return -1;
    root_filter(paths, &o->path_args);
    glob_filter(paths, &o->include, &o->exclude);
    return 0;
}

/* Lines and tokens that would ACTUALLY BE SENT for a file: the outline when
 * --outline is on and the file parses, otherwise the content.
 *
 * Shared by both count paths so `count --outline` cannot silently disagree
 * with `pack --outline` - the same reason --redact is rejected where it would
 * be a no-op.  `ol` receives the outline when one was built. */
static void count_of(const pack_opts *o, const char *path, const char *data,
    size_t len, size_t *lines, size_t *tokens, outline_list *ol)
{
    memset(ol, 0, sizeof(*ol));
    *lines = 0;
    *tokens = 0;
    if (o->outline && outline_build(data, len, path, ol) == 0) {
        strbuf t;
        strbuf_init(&t);
        for (size_t k = 0; k < ol->n; k++) {
            char l[64];
            int m = snprintf(l, sizeof(l), "%6zu: ", ol->v[k].line);
            if (m > 0)
                strbuf_appendn(&t, l, (size_t)m);
            strbuf_append(&t, ol->v[k].decl);
            strbuf_append(&t, "\n");
        }
        *tokens = tokens_estimate(t.data, t.len);
        *lines = ol->n;
        strbuf_free(&t);
        return;
    }
    outline_free(ol);
    for (size_t k = 0; k < len; k++) {
        if (data[k] == '\n')
            (*lines)++;
    }
    *tokens = tokens_estimate(data, len);
}

static int cmd_count_json(const pack_opts *o, const strlist *paths)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *files = cJSON_CreateArray();
    if (!root || !files) {
        cJSON_Delete(root);
        cJSON_Delete(files);
        return 1;
    }
    cJSON_AddItemToObject(root, "files", files);
    unsigned long long tb = 0, tl = 0, tt = 0;
    size_t tp = 0;
    int ntrunc = 0, nread = 0;
    int rc = 0;
    for (size_t i = 0; i < paths->n; i++) {
        char full[4096];
        join_path(full, sizeof(full), o->root, paths->v[i]);
        read_file rf;
        if (read_file_open(full, o->max_file_tokens, &rf) != 0) {
            fprintf(stderr, "cctxpack: cannot read %s\n", paths->v[i]);
            rc = 1;
            break;
        }
        size_t lines = 0, tokens = 0;
        outline_list ol;
        if (!rf.binary)
            count_of(o, paths->v[i], rf.data, rf.len, &lines, &tokens, &ol);
        else
            memset(&ol, 0, sizeof(ol));
        outline_free(&ol);
        cJSON *f = cJSON_CreateObject();
        cJSON_AddItemToObject(f, "path", cJSON_CreateString(paths->v[i]));
        cJSON_AddItemToObject(f, "bytes",
            cJSON_CreateNumber((double)rf.file_size));
        if (rf.binary)
            cJSON_AddItemToObject(f, "binary", cJSON_CreateTrue());
        cJSON_AddItemToObject(f, "lines", cJSON_CreateNumber((double)lines));
        cJSON_AddItemToObject(f, "tokens", cJSON_CreateNumber((double)tokens));
        /* `bytes` above is measured on disk; `lines` and `tokens` are measured
         * on what was actually read.  Those only describe the same bytes when
         * nothing was dropped, so the gap is stated rather than implied. */
        if (rf.truncated) {
            cJSON_AddItemToObject(f, "truncated", cJSON_CreateTrue());
            cJSON_AddItemToObject(f, "truncation",
                cJSON_CreateString(trunc_reason_name(rf.trunc_reason)));
            ntrunc++;
            if (rf.trunc_reason == TRUNC_READ_CAP) {
                nread++;
                /* Only the read cap drops bytes.  Under a token cap every byte
                 * was read and only tokens were cut, so `packed` would just
                 * repeat `bytes` - and the totals only report it when the two
                 * genuinely differ. */
                cJSON_AddItemToObject(f, "bytes_packed",
                    cJSON_CreateNumber((double)rf.packed));
            }
        }
        cJSON_AddItemToArray(files, f);
        tb += rf.file_size;
        tp += rf.packed;
        tl += lines;
        tt += tokens;
        read_file_free(&rf);
    }
    if (rc == 0) {
        cJSON *total = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "total", total);
        cJSON_AddItemToObject(total, "files",
            cJSON_CreateNumber((double)paths->n));
        cJSON_AddItemToObject(total, "bytes", cJSON_CreateNumber((double)tb));
        if (ntrunc) {
            cJSON_AddItemToObject(total, "truncated",
                cJSON_CreateNumber((double)ntrunc));
            /* Reported only when bytes were actually dropped; a token cap
             * leaves every byte read, so packed would equal bytes. */
            if (nread)
                cJSON_AddItemToObject(total, "bytes_packed",
                    cJSON_CreateNumber((double)tp));
        }
        cJSON_AddItemToObject(total, "lines", cJSON_CreateNumber((double)tl));
        cJSON_AddItemToObject(total, "tokens", cJSON_CreateNumber((double)tt));
        char *out = cJSON_PrintUnformatted(root);
        if (!out) {
            rc = 1;
        } else {
            printf("%s\n", out);
            free(out);
        }
    }
    cJSON_Delete(root);
    return rc;
}

static int cmd_count_table(const pack_opts *o, const strlist *paths)
{
    printf("%9s %9s %9s  %-40s  %s\n", "bytes", "lines", "tokens", "path",
        "note");
    unsigned long long tb = 0, tl = 0, tt = 0;
    int ntrunc = 0;
    for (size_t i = 0; i < paths->n; i++) {
        char full[4096];
        join_path(full, sizeof(full), o->root, paths->v[i]);
        read_file rf;
        if (read_file_open(full, o->max_file_tokens, &rf) != 0) {
            fprintf(stderr, "cctxpack: cannot read %s\n", paths->v[i]);
            return 1;
        }
        size_t lines = 0, tokens = 0;
        outline_list ol;
        if (!rf.binary)
            count_of(o, paths->v[i], rf.data, rf.len, &lines, &tokens, &ol);
        else
            memset(&ol, 0, sizeof(ol));
        outline_free(&ol);
        /* A truncated row must say so on the same line: `bytes` is on disk but
         * the line and token counts come from only part of the file, so a
         * reader who skips the note sizes their budget wrong. */
        char note[72];
        if (rf.trunc_reason == TRUNC_READ_CAP)
            snprintf(note, sizeof(note), "read cap: %zu of %llu bytes",
                rf.packed, (unsigned long long)rf.file_size);
        else if (rf.trunc_reason == TRUNC_TOKEN_CAP)
            snprintf(note, sizeof(note), "token cap: %llu est. tokens",
                (unsigned long long)o->max_file_tokens);
        else
            note[0] = '\0';
        printf("%9llu %9zu %9zu  %-40s  %s\n",
            (unsigned long long)rf.file_size, lines, tokens,
            paths->v[i], note);
        if (rf.truncated)
            ntrunc++;
        tb += rf.file_size;
        tl += lines;
        tt += tokens;
        read_file_free(&rf);
    }
    printf("%9llu %9llu %9llu  %-40s  %s\n", tb, tl, tt, "total",
        ntrunc ? "some files truncated" : "");
    return 0;
}

int cmd_count(const pack_opts *o)
{
    strlist paths;
    strlist_init(&paths);
    if (discover(o, &paths) != 0) {
        strlist_free(&paths);
        return 1;
    }
    files_sort_mode(&paths, o->root, o->sort);
    int rc = o->format == FMT_JSON ? cmd_count_json(o, &paths)
                                   : cmd_count_table(o, &paths);
    strlist_free(&paths);
    return rc;
}

int cmd_tree(const pack_opts *o)
{
    strlist paths;
    strlist_init(&paths);
    if (discover(o, &paths) != 0) {
        strlist_free(&paths);
        return 1;
    }
    files_sort(&paths);
    strbuf b;
    strbuf_init(&b);
    format_tree(&b, paths.v, paths.n, FMT_TXT);
    int rc = 0;
    if (b.len && fwrite(b.data, 1, b.len, stdout) != b.len)
        rc = 1;
    strbuf_free(&b);
    strlist_free(&paths);
    return rc;
}

int cmd_diff(pack_opts *o)
{
    strlist changed;
    strlist_init(&changed);
    char *top = NULL;
    int rc = files_git_status(o->staged, &changed, &top);
    if (rc == FILES_NOGIT) {
        fprintf(stderr, "cctxpack: diff needs a git work tree\n");
        strlist_free(&changed);
        return 2;
    }
    if (rc != 0) {
        strlist_free(&changed);
        return 1;
    }
    strlist_free(&o->path_args);
    o->path_args = changed;
    free((char *)o->root);
    o->root = top;
    free((char *)o->root_display);
    const char *slash = strrchr(top, '/');
    o->root_display = str_dup(slash ? slash + 1 : top);
    if (!o->root_display)
        return 1;
    return pack_run(o);
}
