#include "cli.h"
#include "cmd.h"
#include "config.h"
#include "pack.h"

#include <stdio.h>
#include <string.h>

static int is_command(const char *s)
{
    return strcmp(s, "pack") == 0 || strcmp(s, "count") == 0
        || strcmp(s, "tree") == 0 || strcmp(s, "diff") == 0;
}

int main(int argc, char **argv)
{
    const char *cmd = "pack";
    if (argc > 1 && is_command(argv[1])) {
        cmd = argv[1];
        for (int i = 2; i < argc; i++)
            argv[i - 1] = argv[i];
        argc--;
    }
    pack_opts o;
    memset(&o, 0, sizeof(o));
    int exit_code = 0;
    if (cli_parse(argc, argv, cmd, &o, &exit_code) == CLI_EXIT)
        return exit_code;
    int crc = config_apply(&o);
    if (crc == 2)
        return 2;
    if (crc == -1)
        return 1;
    /* Provenance for the bundle manifest.  Resolved once, from the work tree
     * rather than o->root, so it is correct for diff too (which re-points root
     * at the git top level).  NULL outside a work tree, which renders as
     * "unversioned". */
    o.commit = files_git_commit();
    if (strcmp(cmd, "count") == 0)
        return cmd_count(&o);
    if (strcmp(cmd, "tree") == 0)
        return cmd_tree(&o);
    if (strcmp(cmd, "diff") == 0)
        return cmd_diff(&o);
    return pack_run(&o);
}
