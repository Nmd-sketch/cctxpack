#ifndef CTX_PACK_H
#define CTX_PACK_H

#include "files.h"

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
    int want_report;
    int want_tree;
    const char *output;
    const char *fake_ls;
} pack_opts;

int pack_run(const pack_opts *o);

size_t pack_fit(const size_t *ests, size_t n, size_t remaining,
    unsigned char *included);

#endif
