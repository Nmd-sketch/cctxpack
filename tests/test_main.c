#include "greatest.h"

GREATEST_MAIN_DEFS();

SUITE_EXTERN(util_suite);
SUITE_EXTERN(getopt_suite);

int main(int argc, char **argv)
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(util_suite);
    RUN_SUITE(getopt_suite);
    GREATEST_MAIN_END();
}
