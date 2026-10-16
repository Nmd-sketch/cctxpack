#ifndef CTX_CONFIG_H
#define CTX_CONFIG_H

#include "pack.h"

#include <stddef.h>

/* `.cctxpack.json` (FR-014).  Every key is optional. */
typedef struct {
    int has_budget;
    size_t budget;
    int has_max;
    size_t max_file_tokens;
    int has_tree;
    int tree;
    int has_format;
    int format;
    strlist include;
    strlist exclude;
} config;

void config_init(config *c);
void config_free(config *c);

/* Nearest `.cctxpack.json` at or above the cwd, bounded by the enclosing git
 * work tree (cwd only outside one).  Returns 1 with *path set (owned), 0 when
 * there is none, -1 on allocation failure. */
int config_find(char **path);
/* Parse `path`.  Returns 0, 2 for an invalid document (diagnostic on stderr),
 * or -1 on allocation failure. */
int config_read(const char *path, config *c);
/* Fill options the CLI left unset (the CLI always wins).  Returns 0 or -1. */
int config_merge(const config *c, pack_opts *o);
/* find + read + merge.  Returns 0, 2 or -1. */
int config_apply(pack_opts *o);

#endif
