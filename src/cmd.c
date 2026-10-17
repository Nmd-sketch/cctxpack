#include "cmd.h"

#include "cJSON.h"
#include "format.h"
#include "glob.h"
#include "read.h"
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
        if (!rf.binary) {
            for (size_t k = 0; k < rf.len; k++) {
                if (rf.data[k] == '\n')
                    lines++;
            }
            tokens = tokens_estimate(rf.data, rf.len);
        }
        cJSON *f = cJSON_CreateObject();
        cJSON_AddItemToObject(f, "path", cJSON_CreateString(paths->v[i]));
        cJSON_AddItemToObject(f, "bytes",
            cJSON_CreateNumber((double)rf.file_size));
        if (rf.binary)
            cJSON_AddItemToObject(f, "binary", cJSON_CreateTrue());
        cJSON_AddItemToObject(f, "lines", cJSON_CreateNumber((double)lines));
        cJSON_AddItemToObject(f, "tokens", cJSON_CreateNumber((double)tokens));
        cJSON_AddItemToArray(files, f);
        tb += rf.file_size;
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
    printf("%9s %9s %9s  %s\n", "bytes", "lines", "tokens", "path");
    unsigned long long tb = 0, tl = 0, tt = 0;
    for (size_t i = 0; i < paths->n; i++) {
        char full[4096];
        join_path(full, sizeof(full), o->root, paths->v[i]);
        read_file rf;
        if (read_file_open(full, o->max_file_tokens, &rf) != 0) {
            fprintf(stderr, "cctxpack: cannot read %s\n", paths->v[i]);
            return 1;
        }
        size_t lines = 0, tokens = 0;
        if (!rf.binary) {
            for (size_t k = 0; k < rf.len; k++) {
                if (rf.data[k] == '\n')
                    lines++;
            }
            tokens = tokens_estimate(rf.data, rf.len);
        }
        printf("%9llu %9zu %9zu  %s\n", (unsigned long long)rf.file_size, lines,
            tokens, paths->v[i]);
        tb += rf.file_size;
        tl += lines;
        tt += tokens;
        read_file_free(&rf);
    }
    printf("%9llu %9llu %9llu  %s\n", tb, tl, tt, "total");
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
