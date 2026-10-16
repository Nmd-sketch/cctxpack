#include "greatest.h"

#include <getopt.h>

extern int optreset;

static const struct option long_opts[] = {
    {"budget", required_argument, NULL, 'b'},
    {"format", required_argument, NULL, 'f'},
    {"report", no_argument, NULL, 'r'},
    {NULL, 0, NULL, 0},
};

static void reset(int *argc, char **argv, int n)
{
    *argc = n;
    (void)argv;
    optind = 0;
    optreset = 1;
    opterr = 0;
}

TEST long_option_with_separate_argument(void)
{
    char *argv[] = {"cctxpack", "--budget", "8000", NULL};
    int argc;
    reset(&argc, argv, 3);
    int c = getopt_long(argc, argv, "", long_opts, NULL);
    ASSERT_EQ('b', c);
    ASSERT_STR_EQ("8000", optarg);
    ASSERT(getopt_long(argc, argv, "", long_opts, NULL) == -1);
    PASS();
}

TEST long_option_with_equals_argument(void)
{
    char *argv[] = {"cctxpack", "--format=md", NULL};
    int argc;
    reset(&argc, argv, 2);
    int c = getopt_long(argc, argv, "", long_opts, NULL);
    ASSERT_EQ('f', c);
    ASSERT_STR_EQ("md", optarg);
    ASSERT(getopt_long(argc, argv, "", long_opts, NULL) == -1);
    PASS();
}

TEST unknown_option_returns_question_mark(void)
{
    char *argv[] = {"cctxpack", "--nope", NULL};
    int argc;
    reset(&argc, argv, 2);
    int c = getopt_long(argc, argv, "", long_opts, NULL);
    ASSERT_EQ('?', c);
    PASS();
}

TEST double_dash_terminates_options(void)
{
    char *argv[] = {"cctxpack", "--report", "--", "file.c", NULL};
    int argc;
    reset(&argc, argv, 4);
    int c = getopt_long(argc, argv, "", long_opts, NULL);
    ASSERT_EQ('r', c);
    ASSERT(getopt_long(argc, argv, "", long_opts, NULL) == -1);
    ASSERT_STR_EQ("file.c", argv[optind]);
    PASS();
}

TEST non_option_arguments_are_permuted(void)
{
    char *argv[] = {"cctxpack", "file.c", "--budget", "8000", "other.c", NULL};
    int argc;
    reset(&argc, argv, 5);
    int c = getopt_long(argc, argv, "", long_opts, NULL);
    ASSERT_EQ('b', c);
    ASSERT_STR_EQ("8000", optarg);
    ASSERT(getopt_long(argc, argv, "", long_opts, NULL) == -1);
    ASSERT_STR_EQ("file.c", argv[optind]);
    PASS();
}

SUITE(getopt_suite)
{
    RUN_TEST(long_option_with_separate_argument);
    RUN_TEST(long_option_with_equals_argument);
    RUN_TEST(unknown_option_returns_question_mark);
    RUN_TEST(double_dash_terminates_options);
    RUN_TEST(non_option_arguments_are_permuted);
}
