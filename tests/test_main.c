#include "greatest.h"

GREATEST_MAIN_DEFS();

SUITE_EXTERN(util_suite);
SUITE_EXTERN(getopt_suite);
SUITE_EXTERN(files_suite);
SUITE_EXTERN(read_suite);
SUITE_EXTERN(tokens_suite);
SUITE_EXTERN(format_suite);
SUITE_EXTERN(config_suite);
SUITE_EXTERN(pack_suite);
SUITE_EXTERN(redact_suite);
SUITE_EXTERN(cli_suite);
SUITE_EXTERN(e2e_suite);

int main(int argc, char **argv)
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(util_suite);
    RUN_SUITE(getopt_suite);
    RUN_SUITE(files_suite);
    RUN_SUITE(read_suite);
    RUN_SUITE(tokens_suite);
    RUN_SUITE(format_suite);
    RUN_SUITE(config_suite);
    RUN_SUITE(pack_suite);
RUN_SUITE(redact_suite);
    RUN_SUITE(cli_suite);
    RUN_SUITE(e2e_suite);
    GREATEST_MAIN_END();
}
