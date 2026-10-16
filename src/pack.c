#include "pack.h"

#include "format.h"
#include "glob.h"
#include "read.h"
#include "tokens.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define path_eq(a, b) (_stricmp((a), (b)) == 0)
#else
#define path_eq(a, b) (strcmp((a), (b)) == 0)
#endif

typedef struct {
    const char *path;
    size_t est;
    uint64_t size;
    int binary;
    int truncated;
    int included;
} pack_item;

size_t pack_fit(const size_t *ests, size_t n, size_t remaining,
    unsigned char *included)
{
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        if (ests[i] <= remaining) {
            included[i] = 1;
            remaining -= ests[i];
            count++;
        } else {
            included[i] = 0;
        }
    }
    return count;
}

static void join_path(char *buf, size_t cap, const char *root, const char *rel)
{
    snprintf(buf, cap, "%s/%s", root, rel);
}

static void report(const pack_opts *o, const pack_item *items, size_t n,
    size_t included, size_t truncated, size_t used)
{
    fprintf(stderr, "cctxpack: included %zu files", included);
    if (truncated)
        fprintf(stderr, " (%zu truncated)", truncated);
    fprintf(stderr, ", excluded %zu\n", n - included);
    for (size_t i = 0; i < n; i++) {
        if (!items[i].included) {
            fprintf(stderr, "cctxpack: excluded %s (%s)\n", items[i].path,
                items[i].binary ? "binary" : "over budget");
        }
    }
    if (o->has_budget)
        fprintf(stderr, "cctxpack: est. tokens %zu/%zu (budget)\n", used,
            o->budget);
    else
        fprintf(stderr, "cctxpack: est. tokens %zu\n", used);
}

int pack_run(const pack_opts *o)
{
    int rc = 1;
    strlist paths;
    strlist inc_paths;
    strbuf header, tree_all, tree_in;
    pack_item *items = NULL;
    size_t *ests = NULL;
    unsigned char *incl = NULL;
    size_t truncated = 0;
    size_t used = 0;

    strlist_init(&paths);
    strlist_init(&inc_paths);
    strbuf_init(&header);
    strbuf_init(&tree_all);
    strbuf_init(&tree_in);

    int used_git = 0;
    if (files_discover(o->root, o->fake_ls, &paths, &used_git) != 0)
        goto cleanup;
    root_filter(&paths, &o->path_args);
    glob_filter(&paths, &o->include, &o->exclude);
    files_sort(&paths);

    format_header(&header, o->root_display);
    if (o->want_tree && paths.n > 0)
        format_tree(&tree_all, paths.v, paths.n);
    size_t reserve = tokens_estimate(header.data, header.len)
        + (tree_all.len ? tokens_estimate(tree_all.data, tree_all.len) : 0);

    items = calloc(paths.n ? paths.n : 1, sizeof(*items));
    ests = calloc(paths.n ? paths.n : 1, sizeof(*ests));
    incl = calloc(paths.n ? paths.n : 1, 1);
    if (!items || !ests || !incl)
        goto cleanup;

    for (size_t i = 0; i < paths.n; i++) {
        char full[4096];
        join_path(full, sizeof(full), o->root, paths.v[i]);
        read_file rf;
        if (read_file_open(full, o->max_file_tokens, &rf) != 0) {
            fprintf(stderr, "cctxpack: cannot read %s\n", paths.v[i]);
            goto cleanup;
        }
        items[i].path = paths.v[i];
        items[i].binary = rf.binary;
        items[i].truncated = rf.truncated;
        items[i].size = rf.file_size;
        strbuf sec;
        strbuf_init(&sec);
        if (rf.binary) {
            format_binary(&sec, paths.v[i], (size_t)rf.file_size);
        } else {
            format_section(&sec, paths.v[i], rf.data, rf.len);
        }
        ests[i] = tokens_estimate(sec.data, sec.len);
        strbuf_free(&sec);
        read_file_free(&rf);
    }

    size_t remaining = 0;
    size_t count;
    if (o->has_budget) {
        remaining = o->budget > reserve ? o->budget - reserve : 0;
        count = pack_fit(ests, paths.n, remaining, incl);
        for (size_t i = 0; i < paths.n; i++)
            items[i].included = incl[i] != 0;
        if (count == 0) {
            fprintf(stderr,
                "cctxpack: nothing fits the budget (reserve %zu, budget %zu)\n",
                reserve, o->budget);
            report(o, items, paths.n, 0, 0, 0);
            rc = 3;
            goto cleanup;
        }
    } else {
        count = paths.n;
        for (size_t i = 0; i < paths.n; i++) {
            items[i].included = 1;
            incl[i] = 1;
        }
    }

    for (size_t i = 0; i < paths.n; i++) {
        if (incl[i])
            strlist_push(&inc_paths, paths.v[i]);
    }
    if (o->want_tree && inc_paths.n > 0)
        format_tree(&tree_in, inc_paths.v, inc_paths.n);

    used = tokens_estimate(header.data, header.len)
        + (tree_in.len ? tokens_estimate(tree_in.data, tree_in.len) : 0);
    for (size_t i = 0; i < paths.n; i++) {
        if (incl[i]) {
            used += ests[i];
            if (items[i].truncated)
                truncated++;
        }
    }

    FILE *out = stdout;
    if (o->output) {
        char *abs_out = path_abs(o->output);
        for (size_t i = 0; abs_out && i < paths.n; i++) {
            if (!incl[i])
                continue;
            char full[4096];
            join_path(full, sizeof(full), o->root, items[i].path);
            char *abs_full = path_abs(full);
            if (abs_full && path_eq(abs_out, abs_full)) {
                fprintf(stderr, "cctxpack: -o %s overwrites an input file\n",
                    o->output);
                free(abs_full);
                free(abs_out);
                rc = 2;
                goto cleanup;
            }
            free(abs_full);
        }
        free(abs_out);
        out = path_open(o->output, "wb");
        if (!out) {
            fprintf(stderr, "cctxpack: cannot write %s\n", o->output);
            goto cleanup;
        }
    }

    rc = 0;
    fwrite(header.data, 1, header.len, out);
    fwrite(tree_in.data, 1, tree_in.len, out);
    for (size_t i = 0; i < paths.n; i++) {
        if (!incl[i])
            continue;
        strbuf sec;
        strbuf_init(&sec);
        read_file rf;
        memset(&rf, 0, sizeof(rf));
        if (items[i].binary) {
            format_binary(&sec, items[i].path, (size_t)items[i].size);
        } else {
            char full[4096];
            join_path(full, sizeof(full), o->root, items[i].path);
            if (read_file_open(full, o->max_file_tokens, &rf) != 0) {
                fprintf(stderr, "cctxpack: cannot read %s\n", items[i].path);
                strbuf_free(&sec);
                rc = 1;
                break;
            }
            format_section(&sec, items[i].path, rf.data, rf.len);
            read_file_free(&rf);
        }
        fwrite(sec.data, 1, sec.len, out);
        strbuf_free(&sec);
    }
    if (out != stdout && fclose(out) != 0 && rc == 0)
        rc = 1;
    if (rc == 0 && (o->want_report || o->has_budget))
        report(o, items, paths.n, count, truncated, used);

cleanup:
    free(items);
    free(ests);
    free(incl);
    strlist_free(&inc_paths);
    strlist_free(&paths);
    strbuf_free(&header);
    strbuf_free(&tree_all);
    strbuf_free(&tree_in);
    return rc;
}
