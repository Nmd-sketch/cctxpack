#include "greatest.h"

#include "cli.h"
#include "helpers.h"
#include "pack.h"
#include "tokens.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

static void opts_reset(pack_opts *o)
{
    memset(o, 0, sizeof(*o));
}

TEST parse_no_args_runs_pack(void)
{
    char *argv[] = { "cctxpack" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(1, argv, &o, &code) == CLI_RUN);
    ASSERT_STR_EQ(".", o.root);
    ASSERT(o.root_display != NULL && o.root_display[0] != '\0');
    ASSERT_EQ(1, o.want_tree);
    ASSERT_EQ(0, o.has_budget);
    PASS();
}

TEST parse_help_and_version_exit_zero(void)
{
    char *argv1[] = { "cctxpack", "--help" };
    pack_opts o;
    int code = -1;
    opts_reset(&o);
    ASSERT(cli_parse(2, argv1, &o, &code) == CLI_EXIT);
    ASSERT_EQ(0, code);

    char *argv2[] = { "cctxpack", "--version" };
    code = -1;
    ASSERT(cli_parse(2, argv2, &o, &code) == CLI_EXIT);
    ASSERT_EQ(0, code);
    PASS();
}

TEST parse_unknown_flag_exits_2(void)
{
    char *argv[] = { "cctxpack", "--bogus" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(2, argv, &o, &code) == CLI_EXIT);
    ASSERT_EQ(2, code);
    PASS();
}

TEST parse_bad_numbers_exit_2(void)
{
    char *argv1[] = { "cctxpack", "--budget", "8k" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(3, argv1, &o, &code) == CLI_EXIT);
    ASSERT_EQ(2, code);

    char *argv2[] = { "cctxpack", "--max-file-tokens", "lots" };
    code = 0;
    ASSERT(cli_parse(3, argv2, &o, &code) == CLI_EXIT);
    ASSERT_EQ(2, code);
    PASS();
}

TEST parse_missing_path_exits_2(void)
{
    char *argv[] = { "cctxpack", "no_such_dir_xyz" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(2, argv, &o, &code) == CLI_EXIT);
    ASSERT_EQ(2, code);
    PASS();
}

TEST parse_include_repeatable(void)
{
    char *argv[] = { "cctxpack", "--include", "src/**", "--include", "*.md" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(5, argv, &o, &code) == CLI_RUN);
    ASSERT_EQ(2, (int)o.include.n);
    ASSERT_STR_EQ("src/**", o.include.v[0]);
    ASSERT_STR_EQ("*.md", o.include.v[1]);
    PASS();
}

TEST parse_budget_and_notree(void)
{
    char *argv[] = { "cctxpack", "--budget", "8000", "--no-tree" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(4, argv, &o, &code) == CLI_RUN);
    ASSERT_EQ(1, o.has_budget);
    ASSERT((size_t)8000 == o.budget);
    ASSERT_EQ(0, o.want_tree);
    PASS();
}

static const char *bin(void)
{
    return CTX_E2E_BIN;
}

/* All e2e outputs live under one scratch directory in the test process cwd
 * (build/tests under ctest), created lazily and removed around the suite.  The
 * returned path is absolute so it survives the chdir into the fixture repo. */
static char *scratch(const char *name)
{
    char joined[512];
    ctx_mkdir("ctx_e2e_tmp");
    snprintf(joined, sizeof(joined), "ctx_e2e_tmp/%s", name);
    return path_abs(joined);
}

TEST e2e_version_and_help(void)
{
    char *out = scratch("e2e_out.txt");
    char *err = scratch("e2e_err.txt");
    ASSERT(out && err);
    ASSERT_EQ(0, test_run(bin(), "--version", out, err));
    char *buf = test_read_all(out, NULL);
    ASSERT(buf != NULL);
    ASSERT(strstr(buf, "cctxpack ") != NULL);
    free(buf);
    ASSERT_EQ(0, test_run(bin(), "--help", out, err));
    buf = test_read_all(out, NULL);
    ASSERT(buf != NULL);
    ASSERT(strstr(buf, "--budget") != NULL);
    ASSERT(strstr(buf, "cctxpack diff") != NULL);
    free(buf);
    free(out);
    free(err);
    PASS();
}

TEST e2e_usage_errors(void)
{
    char *out = scratch("e2e_out.txt");
    char *err = scratch("e2e_err.txt");
    ASSERT(out && err);
    ASSERT_EQ(2, test_run(bin(), "--nope", out, err));
    ASSERT_EQ(2, test_run(bin(), "count", out, err));
    char *buf = test_read_all(err, NULL);
    ASSERT(buf != NULL);
    ASSERT(strstr(buf, "not implemented yet") != NULL);
    free(buf);
    free(out);
    free(err);
    PASS();
}

TEST e2e_pack_fixture(void)
{
    char *out = scratch("e2e_bundle.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(out && err && nul);
    char *saved = path_cwd();
    ASSERT(saved != NULL);
    ASSERT(ctx_chdir(CTX_FIXTURE_DIR) == 0);
    char args[512];
    snprintf(args, sizeof(args), ". -o \"%s\"", out);
    int rc = test_run(bin(), args, nul, err);
    ctx_chdir(saved);
    free(saved);
    ASSERT_EQ(0, rc);
    size_t len = 0;
    char *bundle = test_read_all(out, &len);
    ASSERT(bundle != NULL);
    ASSERT(strstr(bundle, "### README.md") != NULL);
    ASSERT(strstr(bundle, "```text") != NULL);
    ASSERT(strstr(bundle, "«binary: data/blob.bin") != NULL);
    ASSERT(strstr(bundle, "ignored.log") == NULL);
    free(bundle);
    free(out);
    free(err);
    free(nul);
    PASS();
}

TEST e2e_gitignore_respected(void)
{
    /* A throwaway work tree with one tracked file and one ignored file proves
     * git-ignore semantics without depending on the enclosing repository. */
    char *dir = scratch("probe");
    char *out = scratch("gi_out.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && out && err && nul);
    ctx_mkdir(dir); /* ok if it already exists */
    char kp[1024], sp[1024], gp[1024];
    snprintf(kp, sizeof(kp), "%s/keep.txt", dir);
    snprintf(sp, sizeof(sp), "%s/secret.log", dir);
    snprintf(gp, sizeof(gp), "%s/.gitignore", dir);
    ASSERT(test_write_file(kp, "kept file\n", 10) == 0);
    ASSERT(test_write_file(sp, "ignored file\n", 13) == 0);
    ASSERT(test_write_file(gp, "*.log\n", 6) == 0);

    char *saved = path_cwd();
    ASSERT(saved != NULL);
    ASSERT(ctx_chdir(dir) == 0);
    if (test_system("git init -q") != 0) {
        ctx_chdir(saved);
        free(saved);
        SKIPm("git unavailable");
    }
    ASSERT(test_system("git -c core.autocrlf=false add -A") == 0);
    char args[512];
    snprintf(args, sizeof(args), ". -o \"%s\"", out);
    int rc = test_run(bin(), args, nul, err);
    ctx_chdir(saved);
    free(saved);
    ASSERT_EQ(0, rc);
    size_t len = 0;
    char *b = test_read_all(out, &len);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "keep.txt") != NULL);
    ASSERT(strstr(b, "secret.log") == NULL);
    free(b);
    free(out);
    free(err);
    free(nul);
    free(dir);
    PASS();
}

TEST e2e_deterministic_output(void)
{
    char *out1 = scratch("e2e_det1.md");
    char *out2 = scratch("e2e_det2.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(out1 && out2 && err && nul);
    char *saved = path_cwd();
    ASSERT(saved != NULL);
    ASSERT(ctx_chdir(CTX_FIXTURE_DIR) == 0);
    char args[1024];
    snprintf(args, sizeof(args), ". -o \"%s\"", out1);
    int rc1 = test_run(bin(), args, nul, err);
    snprintf(args, sizeof(args), ". -o \"%s\"", out2);
    int rc2 = test_run(bin(), args, nul, err);
    ctx_chdir(saved);
    free(saved);
    ASSERT_EQ(0, rc1);
    ASSERT_EQ(0, rc2);
    size_t len1 = 0, len2 = 0;
    char *b1 = test_read_all(out1, &len1);
    char *b2 = test_read_all(out2, &len2);
    ASSERT(b1 != NULL && b2 != NULL);
    ASSERT_EQ((int)len1, (int)len2);
    ASSERT_MEM_EQ(b1, b2, len1);
    free(b1);
    free(b2);
    free(out1);
    free(out2);
    free(err);
    free(nul);
    PASS();
}

TEST e2e_budget_invariant_and_exit3(void)
{
    char *out = scratch("e2e_budget.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(out && err && nul);
    char *saved = path_cwd();
    ASSERT(saved != NULL);
    ASSERT(ctx_chdir(CTX_FIXTURE_DIR) == 0);
    test_rm_rf(out);
    char args[1024];
    snprintf(args, sizeof(args), ". --budget 50000 -o \"%s\"", out);
    int rc = test_run(bin(), args, nul, err);
    ctx_chdir(saved);
    free(saved);
    ASSERT_EQ(0, rc);
    size_t len = 0;
    char *bundle = test_read_all(out, &len);
    ASSERT(bundle != NULL);
    ASSERT(tokens_estimate(bundle, len) <= 50000);
    free(bundle);
    char *buf = test_read_all(err, NULL);
    ASSERT(buf != NULL);
    ASSERT(strstr(buf, "cctxpack: included") != NULL);
    free(buf);

    saved = path_cwd();
    ASSERT(ctx_chdir(CTX_FIXTURE_DIR) == 0);
    test_rm_rf(out);
    snprintf(args, sizeof(args), ". --budget 5 -o \"%s\"", out);
    rc = test_run(bin(), args, nul, err);
    ctx_chdir(saved);
    free(saved);
    ASSERT_EQ(3, rc);
    FILE *f = fopen(out, "rb");
    ASSERT(f == NULL);
    free(out);
    free(err);
    free(nul);
    PASS();
}

TEST e2e_o_guard_refuses_input_overwrite(void)
{
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    char *saved = path_cwd();
    ASSERT(saved && err && nul);
    ASSERT(ctx_chdir(CTX_FIXTURE_DIR) == 0);
    int rc = test_run(bin(), "-o README.md", nul, err);
    ctx_chdir(saved);
    free(saved);
    ASSERT_EQ(2, rc);
    size_t len = 0;
    char *nul2 = scratch("e2e_null.txt");
    char *buf = test_read_all(nul2, &len);
    free(buf);
    free(nul2);
    free(err);
    free(nul);
    PASS();
}

SUITE(cli_suite)
{
    RUN_TEST(parse_no_args_runs_pack);
    RUN_TEST(parse_help_and_version_exit_zero);
    RUN_TEST(parse_unknown_flag_exits_2);
    RUN_TEST(parse_bad_numbers_exit_2);
    RUN_TEST(parse_missing_path_exits_2);
    RUN_TEST(parse_include_repeatable);
    RUN_TEST(parse_budget_and_notree);
}

SUITE(e2e_suite)
{
    test_rm_rf("ctx_e2e_tmp");
    RUN_TEST(e2e_version_and_help);
    RUN_TEST(e2e_usage_errors);
    RUN_TEST(e2e_pack_fixture);
    RUN_TEST(e2e_gitignore_respected);
    RUN_TEST(e2e_deterministic_output);
    RUN_TEST(e2e_budget_invariant_and_exit3);
    RUN_TEST(e2e_o_guard_refuses_input_overwrite);
    test_rm_rf("ctx_e2e_tmp");
}
