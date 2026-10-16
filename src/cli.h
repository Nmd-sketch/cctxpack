#ifndef CTX_CLI_H
#define CTX_CLI_H

#include "pack.h"

typedef enum {
    CLI_RUN = 0,
    CLI_EXIT = 1
} cli_action;

/* `cmd` is the resolved subcommand name ("pack", "count", "tree" or "diff"). */
cli_action cli_parse(int argc, char **argv, const char *cmd, pack_opts *o,
    int *exit_code);
void cli_help(void);

#endif
