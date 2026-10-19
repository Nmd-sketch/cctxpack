#ifndef CTX_PACK_H
#define CTX_PACK_H

#include "files.h"
#include "score.h"

#include <stddef.h>

typedef struct {
    const char *root;
    const char *root_display;
    strlist path_args;
    strlist include;
    strlist exclude;
    size_t budget;
    int has_budget;
    size_t max_file_tokens;
    int max_tokens_set;
    int want_report;
    int want_tree;
    int tree_set;
    int format;
    int fmt_set;
    int sort;
    int sort_set;
    int clipboard;
    int staged;
    int patch;
    int redact;
    int outline;
    int show_config;
    const char *commit;
    const char *query;
    const char *output;
    const char *fake_ls;
} pack_opts;

int pack_run(const pack_opts *o);

/* pack_fit() lives in fit.h. */

/* Relevance score of `path` for a whitespace-separated `query`: occurrences of
 * each term, counting a hit in the basename twice (FR-015). */
size_t pack_score(const char *path, const char *query);

#endif
