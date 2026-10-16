#include "cli.h"
#include "pack.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc > 1) {
        const char *cmd = argv[1];
        if (strcmp(cmd, "count") == 0 || strcmp(cmd, "tree") == 0
            || strcmp(cmd, "diff") == 0) {
            fprintf(stderr,
                "cctxpack: command '%s' is not implemented yet (planned M3)\n",
                cmd);
            return 2;
        }
        if (strcmp(cmd, "pack") == 0) {
            for (int i = 2; i < argc; i++)
                argv[i - 1] = argv[i];
            argc--;
        }
    }
    pack_opts o;
    memset(&o, 0, sizeof(o));
    int exit_code = 0;
    if (cli_parse(argc, argv, &o, &exit_code) == CLI_EXIT)
        return exit_code;
    return pack_run(&o);
}
