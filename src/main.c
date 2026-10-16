#include <stdio.h>
#include <string.h>

#ifndef CTX_VERSION
#define CTX_VERSION "0.0.0-dev"
#endif

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("cctxpack %s\n", CTX_VERSION);
        return 0;
    }

    fprintf(stderr, "usage: cctxpack <command> [paths...] (scaffold stub, see docs/)\n");
    return argc > 1 ? 2 : 0;
}
