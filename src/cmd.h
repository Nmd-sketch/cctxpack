#ifndef CTX_CMD_H
#define CTX_CMD_H

#include "pack.h"

/* FR-011: per-file bytes/lines/est. tokens plus a total, or JSON with
 * `--format json`. */
int cmd_count(const pack_opts *o);
/* FR-012: render the directory tree. */
int cmd_tree(const pack_opts *o);
/* FR-013: pack the files changed versus HEAD. */
int cmd_diff(pack_opts *o);

#endif
