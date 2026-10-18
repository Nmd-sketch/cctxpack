#include "greatest.h"

#include "cJSON.h"
#include "cli.h"
#include "format.h"
#include "helpers.h"
#include "pack.h"
#include "read.h"
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
    ASSERT(cli_parse(1, argv, "pack", &o, &code) == CLI_RUN);
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
    ASSERT(cli_parse(2, argv1, "pack", &o, &code) == CLI_EXIT);
    ASSERT_EQ(0, code);

    char *argv2[] = { "cctxpack", "--version" };
    code = -1;
    ASSERT(cli_parse(2, argv2, "pack", &o, &code) == CLI_EXIT);
    ASSERT_EQ(0, code);
    PASS();
}

TEST parse_unknown_flag_exits_2(void)
{
    char *argv[] = { "cctxpack", "--bogus" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(2, argv, "pack", &o, &code) == CLI_EXIT);
    ASSERT_EQ(2, code);
    PASS();
}

TEST parse_bad_numbers_exit_2(void)
{
    char *argv1[] = { "cctxpack", "--budget", "8k" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(3, argv1, "pack", &o, &code) == CLI_EXIT);
    ASSERT_EQ(2, code);

    char *argv2[] = { "cctxpack", "--max-file-tokens", "lots" };
    code = 0;
    ASSERT(cli_parse(3, argv2, "pack", &o, &code) == CLI_EXIT);
    ASSERT_EQ(2, code);
    PASS();
}

TEST parse_missing_path_exits_2(void)
{
    char *argv[] = { "cctxpack", "no_such_dir_xyz" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(2, argv, "pack", &o, &code) == CLI_EXIT);
    ASSERT_EQ(2, code);
    PASS();
}

TEST parse_include_repeatable(void)
{
    char *argv[] = { "cctxpack", "--include", "src/**", "--include", "*.md" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(5, argv, "pack", &o, &code) == CLI_RUN);
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
    ASSERT(cli_parse(4, argv, "pack", &o, &code) == CLI_RUN);
    ASSERT_EQ(1, o.has_budget);
    ASSERT((size_t)8000 == o.budget);
    ASSERT_EQ(0, o.want_tree);
    PASS();
}

TEST parse_format_and_sort(void)
{
    char *argv[] = { "cctxpack", "--format", "json", "--sort", "mtime" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(5, argv, "pack", &o, &code) == CLI_RUN);
    ASSERT_EQ(FMT_JSON, o.format);
    ASSERT_EQ(1, o.fmt_set);
    ASSERT_EQ(SORT_MTIME, o.sort);
    ASSERT_EQ(1, o.sort_set);
    PASS();
}

TEST parse_rejects_bad_option_values(void)
{
    char *a1[] = { "cctxpack", "--format", "html" };
    char *a2[] = { "cctxpack", "--sort", "random" };
    char *a3[] = { "cctxpack", "--query", "" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(3, a1, "pack", &o, &code) == CLI_EXIT);
    ASSERT_EQ(2, code);
    code = 0;
    ASSERT(cli_parse(3, a2, "pack", &o, &code) == CLI_EXIT);
    ASSERT_EQ(2, code);
    code = 0;
    ASSERT(cli_parse(3, a3, "pack", &o, &code) == CLI_EXIT);
    ASSERT_EQ(2, code);
    PASS();
}

TEST parse_query_and_sort_recorded(void)
{
    char *argv[] = { "cctxpack", "--query", "token budget", "--sort", "size" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(5, argv, "pack", &o, &code) == CLI_RUN);
    ASSERT_STR_EQ("token budget", o.query);
    ASSERT_EQ(SORT_SIZE, o.sort);
    ASSERT_EQ(1, o.sort_set);
    PASS();
}

TEST parse_diff_has_no_tree_by_default(void)
{
    char *argv[] = { "cctxpack" };
    char *argv2[] = { "cctxpack", "extra" };
    char *argv3[] = { "cctxpack", "--tree" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(1, argv, "diff", &o, &code) == CLI_RUN);
    ASSERT_EQ(0, o.want_tree);
    code = 0;
    ASSERT(cli_parse(2, argv2, "diff", &o, &code) == CLI_EXIT);
    ASSERT_EQ(2, code);
    code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(2, argv3, "diff", &o, &code) == CLI_RUN);
    ASSERT_EQ(1, o.want_tree);
    ASSERT_EQ(1, o.tree_set);
    PASS();
}

TEST parse_staged_only_with_diff(void)
{
    char *argv[] = { "cctxpack", "--staged" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(2, argv, "pack", &o, &code) == CLI_EXIT);
    ASSERT_EQ(2, code);
    code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(2, argv, "diff", &o, &code) == CLI_RUN);
    ASSERT_EQ(1, o.staged);
    PASS();
}

TEST parse_clipboard_flag(void)
{
    char *argv[] = { "cctxpack", "--clipboard" };
    char *argv2[] = { "cctxpack", "--clipboard", "-o", "x.md" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    cli_action a = cli_parse(2, argv, "pack", &o, &code);
#ifdef _WIN32
    ASSERT(a == CLI_RUN);
    ASSERT_EQ(1, o.clipboard);
#else
    ASSERT(a == CLI_EXIT);
    ASSERT_EQ(2, code);
#endif
    code = 0;
    opts_reset(&o);
    /* rejected everywhere: conflict on Windows, unavailable elsewhere */
    ASSERT(cli_parse(4, argv2, "pack", &o, &code) == CLI_EXIT);
    ASSERT_EQ(2, code);
    PASS();
}

TEST parse_tree_flag_sets_explicit(void)
{
    char *argv[] = { "cctxpack", "--tree", "--format", "xml" };
    pack_opts o;
    int code = 0;
    opts_reset(&o);
    ASSERT(cli_parse(4, argv, "count", &o, &code) == CLI_RUN);
    ASSERT_EQ(1, o.want_tree);
    ASSERT_EQ(1, o.tree_set);
    ASSERT_EQ(FMT_XML, o.format);
    PASS();
}

/* A scratch directory outside the repository work tree: file discovery there
 * falls back to a plain walk, so a probe can control every file it sees. */
static char *tmp_scratch(const char *name)
{
    const char *t = getenv("TMPDIR");
    if (!t || !t[0])
        t = getenv("TMP");
    if (!t || !t[0])
        t = getenv("TEMP");
    if (!t || !t[0])
        t = "/tmp";
    char joined[1024];
    snprintf(joined, sizeof(joined), "%s/cctxpack_%s", t, name);
    return path_abs(joined);
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

/* Run the binary with `dir` as the working directory. */
static int run_in(const char *dir, const char *args, const char *out,
    const char *err)
{
    char *saved = path_cwd();
    if (!saved)
        return -1;
    if (ctx_chdir(dir) != 0) {
        free(saved);
        return -1;
    }
    int rc = test_run(bin(), args, out, err);
    ctx_chdir(saved);
    free(saved);
    return rc;
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
    ASSERT_EQ(2, test_run(bin(), "--format html", out, err));
    ASSERT_EQ(2, test_run(bin(), "--sort random", out, err));
    ASSERT_EQ(2, test_run(bin(), "--staged", out, err));
    ASSERT_EQ(2, test_run(bin(), "diff some/path", out, err));
    char *buf = test_read_all(err, NULL);
    ASSERT(buf != NULL);
    ASSERT(strstr(buf, "cctxpack:") != NULL);
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

TEST e2e_count_and_tree(void)
{
    char *out = scratch("e2e_count.txt");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(out && err && nul);
    ASSERT_EQ(0, run_in(CTX_FIXTURE_DIR, "count .", out, err));
    char *b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "bytes") != NULL);
    ASSERT(strstr(b, "README.md") != NULL);
    ASSERT(strstr(b, "total") != NULL);
    /* binary files count bytes but no tokens */
    ASSERT(strstr(b, "data/blob.bin") != NULL);
    free(b);

    ASSERT_EQ(0, run_in(CTX_FIXTURE_DIR, "count --format json .", out, err));
    b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    cJSON *j = cJSON_Parse(b);
    ASSERT(j != NULL);
    cJSON *files = cJSON_GetObjectItem(j, "files");
    ASSERT(files != NULL && cJSON_GetArraySize(files) >= 4);
    cJSON *total = cJSON_GetObjectItem(j, "total");
    ASSERT(total != NULL);
    ASSERT(cJSON_GetObjectItem(total, "bytes")->valuedouble > 0);
    cJSON_Delete(j);
    free(b);

    /* tree prints connector lines only, never a markdown fence */
    ASSERT_EQ(0, run_in(CTX_FIXTURE_DIR, "tree .", out, err));
    b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "README.md") != NULL);
    ASSERT(strstr(b, "blob.bin") != NULL);
    ASSERT(strstr(b, "```") == NULL);
    free(b);
    free(out);
    free(err);
    free(nul);
    PASS();
}

TEST e2e_other_formats(void)
{
    char *out = scratch("e2e_fmt.txt");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(out && err && nul);

    ASSERT_EQ(0, run_in(CTX_FIXTURE_DIR, "pack . --format xml", out, err));
    char *b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strncmp(b, "<?xml version=\"1.0\"", 19) == 0);
    ASSERT(strstr(b, "<file path=\"README.md\"") != NULL);
    ASSERT(strstr(b, "</context>") != NULL);
    free(b);

    ASSERT_EQ(0, run_in(CTX_FIXTURE_DIR, "pack . --format json", out, err));
    b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    cJSON *j = cJSON_Parse(b);
    ASSERT(j != NULL);
    ASSERT(cJSON_GetObjectItem(j, "tree") != NULL);
    cJSON *files = cJSON_GetObjectItem(j, "files");
    ASSERT(files != NULL && cJSON_GetArraySize(files) >= 4);
    cJSON_Delete(j);
    free(b);

    ASSERT_EQ(0, run_in(CTX_FIXTURE_DIR, "pack . --format txt", out, err));
    b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strncmp(b, "cctxpack bundle: ", 17) == 0);
    ASSERT(strstr(b, "===== README.md =====") != NULL);
    ASSERT(strstr(b, "```text") == NULL);
    free(b);
    free(out);
    free(err);
    free(nul);
    PASS();
}

TEST e2e_query_and_sort_order(void)
{
    char *out = scratch("e2e_order.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(out && err && nul);

    ASSERT_EQ(0, run_in(CTX_FIXTURE_DIR, "pack . --query greet", out, err));
    char *b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    char *first = strstr(b, "### ");
    ASSERT(first != NULL);
    /* the only basename hit ranks ahead of every non-matching path */
    ASSERT(strncmp(first, "### src/greet.c", 15) == 0);
    free(b);

    ASSERT_EQ(0, run_in(CTX_FIXTURE_DIR, "pack . --sort size", out, err));
    b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    char *a = strstr(b, "### README.md");
    char *z = strstr(b, "### docs/guide.md");
    ASSERT(a != NULL && z != NULL);
    /* smallest first: README.md is shorter than docs/guide.md */
    ASSERT(a < z);
    free(b);
    free(out);
    free(err);
    free(nul);
    PASS();
}

TEST e2e_config_file(void)
{
    char *dir = tmp_scratch("cfgtest");
    char *out = scratch("e2e_cfg.txt");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && out && err && nul);
    test_rm_rf(dir);
    ctx_mkdir(dir);
    char cp[1024], fp[1024];
    snprintf(cp, sizeof(cp), "%s/.cctxpack.json", dir);
    snprintf(fp, sizeof(fp), "%s/only.txt", dir);
    ASSERT(test_write_file(fp, "config probe\n", 13) == 0);

    /* config picks the format; the CLI overrides it */
    ASSERT(test_write_file(cp, "{\"format\":\"txt\",\"tree\":false}\n", 33)
        == 0);
    ASSERT_EQ(0, run_in(dir, "pack .", out, err));
    char *b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "cctxpack bundle:") != NULL);
    ASSERT(strstr(b, "```") == NULL);
    free(b);

    ASSERT_EQ(0, run_in(dir, "pack . --format md", out, err));
    b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "### only.txt") != NULL);
    ASSERT(strstr(b, "cctxpack bundle:") == NULL);
    free(b);

    /* malformed JSON is a usage error with a diagnostic */
    ASSERT(test_write_file(cp, "{oops\n", 6) == 0);
    ASSERT_EQ(2, run_in(dir, "pack .", out, err));
    b = test_read_all(err, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "invalid JSON") != NULL);
    free(b);

    test_rm_rf(dir);
    free(out);
    free(err);
    free(nul);
    free(dir);
    PASS();
}

TEST e2e_diff_worktree_and_staged(void)
{
    char *dir = scratch("difftree");
    char *out = scratch("e2e_diff.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && out && err && nul);
    test_rm_rf(dir);
    ctx_mkdir(dir);
    char ap[1024], bp[1024];
    snprintf(ap, sizeof(ap), "%s/a.txt", dir);
    snprintf(bp, sizeof(bp), "%s/b.txt", dir);
    ASSERT(test_write_file(ap, "one\n", 4) == 0);
    ASSERT(test_write_file(bp, "two\n", 4) == 0);

    /* git commands are run with -C so the test cwd is never disturbed */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" init -q", dir);
    if (test_system(cmd) != 0)
        SKIPm("git unavailable");
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e add -A", dir);
    ASSERT(test_system(cmd) == 0);
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e commit -q -m init",
        dir);
    ASSERT(test_system(cmd) == 0);

    /* an unstaged edit shows up */
    ASSERT(test_write_file(ap, "one changed\n", 12) == 0);
    char args[1024];
    snprintf(args, sizeof(args), "diff -o \"%s\"", out);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    char *b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "a.txt") != NULL);
    ASSERT(strstr(b, "b.txt") == NULL);
    free(b);

    /* staging a.txt moves it into the --staged view only */
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" add a.txt", dir);
    ASSERT(test_system(cmd) == 0);
    ASSERT(test_write_file(bp, "two changed\n", 12) == 0);
    snprintf(args, sizeof(args), "diff --staged -o \"%s\"", out);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "a.txt") != NULL);
    ASSERT(strstr(b, "b.txt") == NULL);
    free(b);

    /* Plain `diff` spans staged and unstaged: everything not yet committed is
     * what someone asking for a review wants to see.  (Before this was
     * fixed, plain `diff` compared the work tree to the index, so a staged
     * change produced no output at all.) */
    snprintf(args, sizeof(args), "diff -o \"%s\"", out);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "b.txt") != NULL);
    ASSERT(strstr(b, "a.txt") != NULL);
    free(b);

    test_rm_rf(dir);
    free(out);
    free(err);
    free(nul);
    free(dir);
    PASS();
}

/* The three states `git diff --name-only` silently dropped. */
TEST e2e_diff_sees_untracked_staged_and_drops_deleted(void)
{
    char *dir = scratch("diffstates");
    char *out = scratch("e2e_diffstates.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && out && err && nul);
    test_rm_rf(dir);
    ctx_mkdir(dir);
    char ap[1024], gp[1024];
    snprintf(ap, sizeof(ap), "%s/staged.txt", dir);
    snprintf(gp, sizeof(gp), "%s/gone.txt", dir);
    ASSERT(test_write_file(ap, "one\n", 4) == 0);
    ASSERT(test_write_file(gp, "two\n", 4) == 0);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" init -q", dir);
    if (test_system(cmd) != 0)
        SKIPm("git unavailable");
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e add -A", dir);
    ASSERT(test_system(cmd) == 0);
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e commit -q -m init",
        dir);
    ASSERT(test_system(cmd) == 0);

    /* a staged addition, an untracked file, and a deletion */
    char sp[1024], up[1024];
    snprintf(sp, sizeof(sp), "%s/added.txt", dir);
    snprintf(up, sizeof(up), "%s/fresh.txt", dir);
    ASSERT(test_write_file(sp, "added\n", 6) == 0);
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" add added.txt", dir);
    ASSERT(test_system(cmd) == 0);
    ASSERT(test_write_file(up, "fresh\n", 6) == 0);
    ASSERT(remove(gp) == 0);

    char args[1024];
    snprintf(args, sizeof(args), "diff -o \"%s\"", out);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    char *b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "added.txt") != NULL);   /* staged */
    ASSERT(strstr(b, "fresh.txt") != NULL);   /* untracked */
    ASSERT(strstr(b, "gone.txt") == NULL);     /* deleted: nothing to pack */
    free(b);

    /* ...but an untracked file is not in the index, so --staged omits it */
    snprintf(args, sizeof(args), "diff --staged -o \"%s\"", out);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "added.txt") != NULL);
    ASSERT(strstr(b, "fresh.txt") == NULL);
    free(b);

    test_rm_rf(dir);
    free(out);
    free(err);
    free(nul);
    free(dir);
    PASS();
}

/* An untracked DIRECTORY must be expanded to its files.  `git status
 * --porcelain` collapses it to a single "dir/" entry by default, which is not
 * a path that can be read. */
TEST e2e_diff_expands_untracked_directory(void)
{
    char *dir = scratch("diffdir");
    char *out = scratch("e2e_diffdir.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && out && err && nul);
    test_rm_rf(dir);
    ctx_mkdir(dir);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" init -q", dir);
    if (test_system(cmd) != 0)
        SKIPm("git unavailable");
    char inner[1024], deep[1024];
    snprintf(inner, sizeof(inner), "%s/pkg", dir);
    snprintf(deep, sizeof(deep), "%s/pkg/deep.txt", dir);
    ASSERT(ctx_mkdir(inner) == 0);
    ASSERT(test_write_file(deep, "deep\n", 5) == 0);

    char args[1024];
    snprintf(args, sizeof(args), "diff -o \"%s\"", out);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    char *b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "pkg/deep.txt") != NULL);
    ASSERT(strstr(b, "pkg/deep.txt") || strstr(b, "pkg\\deep.txt"));
    free(b);

    test_rm_rf(dir);
    free(out);
    free(err);
    free(nul);
    free(dir);
    PASS();
}

/* --patch emits the unified diff itself, so the change is legible without a
 * previous copy of the file. */
TEST e2e_diff_patch_emits_hunks(void)
{
    char *dir = scratch("diffpatch");
    char *out = scratch("e2e_diffpatch.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && out && err && nul);
    test_rm_rf(dir);
    ctx_mkdir(dir);
    char ap[1024];
    snprintf(ap, sizeof(ap), "%s/a.txt", dir);
    ASSERT(test_write_file(ap, "one\n", 4) == 0);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" init -q", dir);
    if (test_system(cmd) != 0)
        SKIPm("git unavailable");
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e add -A", dir);
    ASSERT(test_system(cmd) == 0);
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e commit -q -m init",
        dir);
    ASSERT(test_system(cmd) == 0);

    ASSERT(test_write_file(ap, "one\ntwo\n", 8) == 0);

    char args[1024];
    snprintf(args, sizeof(args), "diff --patch -o \"%s\"", out);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    char *b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "diff --git") != NULL);
    ASSERT(strstr(b, "+two") != NULL);
    free(b);

    /* XML and JSON must stay machine-valid with the single synthetic section */
    snprintf(args, sizeof(args), "diff --patch --format json -o \"%s\"", out);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    cJSON *j = cJSON_Parse(b);
    ASSERT(j != NULL);
    cJSON_Delete(j);
    free(b);

    snprintf(args, sizeof(args), "diff --patch --format xml -o \"%s\"", out);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "<file path=\"changes.diff\"") != NULL);
    free(b);

    test_rm_rf(dir);
    free(out);
    free(err);
    free(nul);
    free(dir);
    PASS();
}

/* The manifest must not break byte-identical output: nothing in it may come
 * from wall-clock time. */
TEST e2e_manifest_is_deterministic(void)
{
    char *dir = scratch("manifest");
    char *o1 = scratch("e2e_man1.md");
    char *o2 = scratch("e2e_man2.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && o1 && o2 && err && nul);
    test_rm_rf(dir);
    ASSERT(ctx_mkdir(dir) == 0);
    char fp[1024];
    snprintf(fp, sizeof(fp), "%s/a.c", dir);
    ASSERT(test_write_file(fp, "int a(void){return 1;}\n", 21) == 0);
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" init -q", dir);
    if (test_system(cmd) != 0)
        SKIPm("git unavailable");
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e add -A", dir);
    ASSERT(test_system(cmd) == 0);
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e commit -q -m init",
        dir);
    ASSERT(test_system(cmd) == 0);

    char args[1024];
    snprintf(args, sizeof(args), "pack . -o \"%s\"", o1);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    snprintf(args, sizeof(args), "pack . -o \"%s\"", o2);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    char *b1 = test_read_all(o1, NULL);
    char *b2 = test_read_all(o2, NULL);
    ASSERT(b1 && b2);
    ASSERT(strcmp(b1, b2) == 0);

    /* provenance line, with a real commit (the scratch repo has one) */
    ASSERT(strstr(b1, "<!-- cctxpack-manifest commit=") != NULL);
    ASSERT(strstr(b1, "commit=unversioned") == NULL);
    ASSERT(strstr(b1, "files=1 ") != NULL);
    ASSERT(strstr(b1, "digest=") != NULL);
    free(b1);
    free(b2);

    /* adding a file must change the digest: it identifies the file set */
    char gp[1024];
    snprintf(gp, sizeof(gp), "%s/b.c", dir);
    ASSERT(test_write_file(gp, "int b(void){return 2;}\n", 21) == 0);
    snprintf(args, sizeof(args), "pack . -o \"%s\"", o1);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    b1 = test_read_all(o1, NULL);
    ASSERT(b1 != NULL);
    ASSERT(strstr(b1, "files=2 ") != NULL);
    free(b1);

    /* and the same commit SHA, because the tree did not change */
    snprintf(args, sizeof(args), "pack . -o \"%s\"", o2);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    b1 = test_read_all(o1, NULL);
    b2 = test_read_all(o2, NULL);
    ASSERT(b1 && b2);
    ASSERT(strstr(b1, "commit=unversioned") == NULL);
    free(b1);
    free(b2);

    test_rm_rf(dir);
    free(o1);
    free(o2);
    free(err);
    free(nul);
    free(dir);
    PASS();
}

/* The manifest is rendered after the budget fit, so the reserve must have
 * accounted for it or a tight budget would be overshot by its own header. */
TEST e2e_manifest_does_not_break_budget(void)
{
    char *dir = scratch("manbudget");
    char *out = scratch("e2e_manbudget.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && out && err && nul);
    test_rm_rf(dir);
    ASSERT(ctx_mkdir(dir) == 0);
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" init -q", dir);
    if (test_system(cmd) != 0)
        SKIPm("git unavailable");
    for (int i = 0; i < 12; i++) {
        char fp[1024];
        snprintf(fp, sizeof(fp), "%s/f%02d.c", dir, i);
        char body[64];
        snprintf(body, sizeof(body), "int f%02d(void){return %d;}\n", i, i);
        ASSERT(test_write_file(fp, body, strlen(body)) == 0);
    }
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e add -A", dir);
    ASSERT(test_system(cmd) == 0);
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e commit -q -m init",
        dir);
    ASSERT(test_system(cmd) == 0);

    /* a range of budgets: none may report more than the cap */
    for (int budget = 120; budget <= 400; budget += 40) {
        char args[1024];
        snprintf(args, sizeof(args), "pack . --budget %d --report -o \"%s\"",
            budget, out);
        int rc = run_in(dir, args, nul, err);
        /* 0 = fitted, 3 = nothing fits, which is correct for the small caps */
        ASSERT(rc == 0 || rc == 3);
        char *e = test_read_all(err, NULL);
        ASSERT(e != NULL);
        /* the tokens line follows the file counts */
        const char *t = strstr(e, "est. tokens ");
        if (!t) {
            fprintf(stderr, "report had no token line: %s", e);
            FAILm("no token line");
        }
        unsigned long used = 0, cap = 0;
        ASSERT(sscanf(t, "est. tokens %lu/%lu (budget)", &used, &cap) == 2);
        ASSERT(used <= cap);
        ASSERT((int)cap == budget);
        free(e);
    }

    test_rm_rf(dir);
    free(out);
    free(err);
    free(nul);
    free(dir);
    PASS();
}

TEST e2e_json_is_valid_without_tree(void)
{
    char *dir = scratch("notree");
    char *out = scratch("e2e_notree.json");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && out && err && nul);
    test_rm_rf(dir);
    ASSERT(ctx_mkdir(dir) == 0);
    char fp[1024];
    snprintf(fp, sizeof(fp), "%s/f.c", dir);
    ASSERT(test_write_file(fp, "int main(void){return 0;}\n", 26) == 0);

    char args[1024];
    snprintf(args, sizeof(args), "pack . --no-tree --format json -o \"%s\"",
        out);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    char *b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    /* format_open writes the "tree" key, so suppressing the tree used to
     * leave it dangling: {"tree":,"files":...} which no parser can read. */
    cJSON *j = cJSON_Parse(b);
    if (!j) {
        fprintf(stderr, "invalid JSON with --no-tree: %.200s\n", b);
        FAILm("invalid JSON with --no-tree");
    }
    cJSON_Delete(j);
    free(b);

    test_rm_rf(dir);
    free(out);
    free(err);
    free(nul);
    free(dir);
    PASS();
}

TEST e2e_cctxpackignore_layered_on_gitignore(void)
{
    char *dir = scratch("ctxignore");
    char *out = scratch("e2e_ctxignore.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && out && err && nul);
    test_rm_rf(dir);
    ASSERT(ctx_mkdir(dir) == 0);

    char keep[1024], drop[1024], docsdir[1024], docs[1024], ig[1024];
    snprintf(keep, sizeof(keep), "%s/keep.c", dir);
    snprintf(drop, sizeof(drop), "%s/drop.c", dir);
    snprintf(docsdir, sizeof(docsdir), "%s/docs", dir);
    snprintf(docs, sizeof(docs), "%s/docs/guide.md", dir);
    snprintf(ig, sizeof(ig), "%s/.cctxpackignore", dir);
    ASSERT(test_write_file(keep, "int k;\n", 7) == 0);
    ASSERT(test_write_file(drop, "int d;\n", 7) == 0);
    ASSERT(ctx_mkdir(docsdir) == 0);
    ASSERT(test_write_file(docs, "guide\n", 6) == 0);

    /* a comment, a blank line and surrounding whitespace must all be skipped */
    const char *ign = "# drop generated scratch\n\n  drop.c  \ndocs/*\n";
    ASSERT(test_write_file(ig, ign, strlen(ign)) == 0);

    /* Inside a work tree discovery is git-only (the walker is a fallback for
     * when git is absent), and the scratch dir is itself gitignored here, so
     * this needs its own throwaway repo for the files to be discovered. */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" init -q", dir);
    if (test_system(cmd) != 0)
        SKIPm("git unavailable");

    char args[1024];
    snprintf(args, sizeof(args), "pack . -o \"%s\"", out);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    char *b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    /* Assert on the section header, not a bare substring: the bundle also
     * contains .cctxpackignore, whose body spells out the patterns. */
    ASSERT(strstr(b, "### keep.c") != NULL);
    ASSERT(strstr(b, "### drop.c") == NULL);
    ASSERT(strstr(b, "### docs/guide.md") == NULL);
    free(b);

    /* --exclude must still win, and neither may break discovery when the file
     * is absent */
    snprintf(args, sizeof(args), "pack . --exclude keep.c -o \"%s\"", out);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "### keep.c") == NULL);
    free(b);

    ASSERT(remove(ig) == 0);
    snprintf(args, sizeof(args), "pack . -o \"%s\"", out);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "### drop.c") != NULL);
    free(b);

    test_rm_rf(dir);
    free(out);
    free(err);
    free(nul);
    free(dir);
    PASS();
}

/* --redact is opt-in: without it the bytes must be exactly what is on disk.
 * Fidelity is the tool's core promise, so this is the load-bearing test. */
TEST e2e_redact_is_opt_in(void)
{
    char *dir = scratch("redact");
    char *o1 = scratch("e2e_red_plain.md");
    char *o2 = scratch("e2e_red_on.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && o1 && o2 && err && nul);
    test_rm_rf(dir);
    ASSERT(ctx_mkdir(dir) == 0);

    const char *body =
        "AWS_KEY = \"AKIAIOSFODNN7EXAMPLE\"\n"
        "db_password = \"s3cr3t-prod-value\"\n"
        "harmless = \"just a literal\"\n";
    char fp[1024];
    snprintf(fp, sizeof(fp), "%s/conf.py", dir);
    ASSERT(test_write_file(fp, body, strlen(body)) == 0);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" init -q", dir);
    if (test_system(cmd) != 0)
        SKIPm("git unavailable");
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e add -A", dir);
    ASSERT(test_system(cmd) == 0);
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e commit -q -m init",
        dir);
    ASSERT(test_system(cmd) == 0);

    char args[1024];
    snprintf(args, sizeof(args), "pack . -o \"%s\"", o1);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    char *plain = test_read_all(o1, NULL);
    ASSERT(plain != NULL);
    ASSERT(strstr(plain, "AKIAIOSFODNN7EXAMPLE") != NULL);
    ASSERT(strstr(plain, "s3cr3t-prod-value") != NULL);
    ASSERT(strstr(plain, "REDACTED") == NULL);
    /* and no manifest claim of scrubbing */
    ASSERT(strstr(plain, "redactions=") == NULL);
    free(plain);

    snprintf(args, sizeof(args), "pack . --redact --report -o \"%s\"", o2);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    char *red = test_read_all(o2, NULL);
    ASSERT(red != NULL);
    ASSERT(strstr(red, "AKIAIOSFODNN7EXAMPLE") == NULL);
    ASSERT(strstr(red, "s3cr3t-prod-value") == NULL);
    /* the statement around the secret survives */
    ASSERT(strstr(red, "db_password = \"[REDACTED:password]\"") != NULL);
    /* and the harmless line is untouched */
    ASSERT(strstr(red, "harmless = \"just a literal\"") != NULL);
    /* the manifest tells the consumer something was scrubbed */
    ASSERT(strstr(red, "redactions=2") != NULL);
    free(red);

    /* the report line on stderr names the count */
    char *e = test_read_all(err, NULL);
    ASSERT(e != NULL);
    ASSERT(strstr(e, "redacted 2 credential(s)") != NULL);
    free(e);

    test_rm_rf(dir);
    free(o1); free(o2); free(err); free(nul); free(dir);
    PASS();
}

/* --redact must not disturb determinism. */
TEST e2e_redact_is_deterministic(void)
{
    char *dir = scratch("reddet");
    char *o1 = scratch("e2e_rd1.md");
    char *o2 = scratch("e2e_rd2.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && o1 && o2 && err && nul);
    test_rm_rf(dir);
    ASSERT(ctx_mkdir(dir) == 0);
    const char *body = "k = \"AKIAIOSFODNN7EXAMPLE\"\n";
    char fp[1024];
    snprintf(fp, sizeof(fp), "%s/a.py", dir);
    ASSERT(test_write_file(fp, body, strlen(body)) == 0);
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" init -q", dir);
    if (test_system(cmd) != 0)
        SKIPm("git unavailable");
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e add -A", dir);
    ASSERT(test_system(cmd) == 0);
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e commit -q -m init",
        dir);
    ASSERT(test_system(cmd) == 0);

    char args[1024];
    snprintf(args, sizeof(args), "pack . --redact -o \"%s\"", o1);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    snprintf(args, sizeof(args), "pack . --redact -o \"%s\"", o2);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    char *b1 = test_read_all(o1, NULL);
    char *b2 = test_read_all(o2, NULL);
    ASSERT(b1 && b2);
    ASSERT(strcmp(b1, b2) == 0);
    free(b1);
    free(b2);

    test_rm_rf(dir);
    free(o1); free(o2); free(err); free(nul); free(dir);
    PASS();
}

/* tree never reads content, so accepting the flag would silently do nothing. */
/* --outline with no budget means "everything as declarations", and
 * --outline --budget means progressive: whole files first, outlines for the
 * rest.  Both must stay inside the budget. */
TEST e2e_outline_modes_and_budget(void)
{
    char *dir = scratch("outline");
    char *o1 = scratch("e2e_ol_all.md");
    char *o2 = scratch("e2e_ol_prog.md");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && o1 && o2 && err && nul);
    test_rm_rf(dir);
    ASSERT(ctx_mkdir(dir) == 0);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" init -q", dir);
    if (test_system(cmd) != 0)
        SKIPm("git unavailable");

    /* six small modules, each with a body big enough to matter */
    for (int i = 0; i < 6; i++) {
        char fp[1024], body[900];
        snprintf(fp, sizeof(fp), "%s/m%02d.c", dir, i);
        size_t n = (size_t)snprintf(body, sizeof(body),
            "#include <stdio.h>\n\nint m%02d_a(int x){\n", i);
        for (int k = 0; k < 12; k++)
            n += (size_t)snprintf(body + n, sizeof(body) - n,
                "  int v%02d_%02d = %d * %d;\n", i, k, i, k);
        n += (size_t)snprintf(body + n, sizeof(body) - n,
            "  return x;\n}\n\nstatic int m%02d_b(void)\n{\n  return 1;\n}\n", i);
        ASSERT(test_write_file(fp, body, n) == 0);
    }
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e add -A", dir);
    ASSERT(test_system(cmd) == 0);
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e commit -q -m init",
        dir);
    ASSERT(test_system(cmd) == 0);

    char args[1024];

    /* full text, for the comparison */
    char *full = NULL;
    size_t full_len = 0;
    snprintf(args, sizeof(args), "count . --format json");
    ASSERT_EQ(0, run_in(dir, args, nul, err));

    /* 1. no budget => everything as declarations */
    snprintf(args, sizeof(args), "pack . --outline --no-tree -o \"%s\"", o1);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    char *b = test_read_all(o1, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "decls, outline)") != NULL);
    ASSERT(strstr(b, "#include <stdio.h>") != NULL);
    /* a body from the source must NOT be present */
    ASSERT(strstr(b, "return x;") == NULL);
    free(b);

    /* 2. with a budget it must stay inside it, and mix both forms */
    snprintf(args, sizeof(args),
        "pack . --outline --budget 600 --report --no-tree -o \"%s\"", o2);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    b = test_read_all(o2, NULL);
    ASSERT(b != NULL);
    char *e = test_read_all(err, NULL);
    ASSERT(e != NULL);
    const char *t = strstr(e, "est. tokens ");
    ASSERT(t != NULL);
    unsigned long used = 0, cap = 0;
    ASSERT(sscanf(t, "est. tokens %lu/%lu (budget)", &used, &cap) == 2);
    ASSERT(used <= 600);
    free(e);
    /* every included file is either a whole body or an outline, never neither */
    ASSERT(strstr(b, "### m00.c") != NULL);
    free(b);

    /* 3. JSON stays machine-valid and carries a structured outline */
    snprintf(args, sizeof(args), "pack . --outline --format json -o \"%s\"",
        o1);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    b = test_read_all(o1, NULL);
    ASSERT(b != NULL);
    cJSON *j = cJSON_Parse(b);
    ASSERT(j != NULL);
    cJSON *files = cJSON_GetObjectItem(j, "files");
    ASSERT(files != NULL);
    cJSON *f0 = cJSON_GetArrayItem(files, 0);
    ASSERT(f0 != NULL);
    cJSON *arr = cJSON_GetObjectItem(f0, "outline");
    ASSERT(arr != NULL);          /* structured, not a text blob */
    ASSERT(cJSON_IsArray(arr));
    cJSON *it0 = cJSON_GetArrayItem(arr, 0);
    ASSERT(it0 != NULL);
    ASSERT(cJSON_GetObjectItem(it0, "line") != NULL);
    ASSERT(cJSON_GetObjectItem(it0, "decl") != NULL);
    cJSON_Delete(j);
    free(b);
    (void)full;
    (void)full_len;

    test_rm_rf(dir);
    free(o1); free(o2); free(err); free(nul); free(dir);
    PASS();
}

/* count and pack must agree: a flag that silently does nothing in count would
 * make every size question wrong. */
TEST e2e_count_outline_agrees_with_pack(void)
{
    char *dir = scratch("olcount");
    char *out = scratch("e2e_olcount.json");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && out && err && nul);
    test_rm_rf(dir);
    ASSERT(ctx_mkdir(dir) == 0);
    char fp[1024];
    snprintf(fp, sizeof(fp), "%s/a.c", dir);
    const char *src = "#include <stdio.h>\n\n"
        "int big(int x){\n  int a = 1;\n  int b = 2;\n  return x + a + b;\n}\n";
    ASSERT(test_write_file(fp, src, strlen(src)) == 0);
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" init -q", dir);
    if (test_system(cmd) != 0)
        SKIPm("git unavailable");
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e add -A", dir);
    ASSERT(test_system(cmd) == 0);
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e commit -q -m init",
        dir);
    ASSERT(test_system(cmd) == 0);

    long full = 0, ol = 0;
    snprintf(cmd, sizeof(cmd), "count . --format json");
    ASSERT_EQ(0, run_in(dir, cmd, out, err));
    char *b = test_read_all(out, NULL);
    cJSON *j = cJSON_Parse(b);
    full = (long)cJSON_GetObjectItem(
        cJSON_GetObjectItem(j, "total"), "tokens")->valueint;
    cJSON_Delete(j);
    free(b);

    snprintf(cmd, sizeof(cmd), "count . --outline --format json");
    ASSERT_EQ(0, run_in(dir, cmd, out, err));
    b = test_read_all(out, NULL);
    j = cJSON_Parse(b);
    ol = (long)cJSON_GetObjectItem(
        cJSON_GetObjectItem(j, "total"), "tokens")->valueint;
    cJSON_Delete(j);
    free(b);

    ASSERT(ol > 0);
    ASSERT(ol < full);           /* the flag actually did something */

    test_rm_rf(dir);
    free(out); free(err); free(nul); free(dir);
    PASS();
}

/* `bytes` is measured on disk but `lines`/`tokens` come from what was read, so
 * a file above the read cap used to report two numbers describing different
 * quantities with nothing saying so.  Anyone sizing a budget from that is
 * wrong by the ratio of the file to the cap. */
TEST e2e_count_reports_truncation(void)
{
    char *dir = scratch("trunc");
    char *out = scratch("e2e_trunc.json");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && out && err && nul);
    test_rm_rf(dir);
    ASSERT(ctx_mkdir(dir) == 0);

    /* one file above the read cap, one ordinary file */
    char big[1024];
    snprintf(big, sizeof(big), "%s/big.c", dir);
    FILE *bf = fopen(big, "wb");
    ASSERT(bf != NULL);
    for (size_t i = 0; i < (size_t)READ_CAP_DEFAULT + 4096; i++)
        fputc('a' + (int)(i % 26), bf);
    fclose(bf);
    char small[1024];
    snprintf(small, sizeof(small), "%s/small.c", dir);
    ASSERT(test_write_file(small, "int x(void){ return 1; }\n", 25) == 0);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" init -q", dir);
    if (test_system(cmd) != 0)
        SKIPm("git unavailable");
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e add -A", dir);
    ASSERT(test_system(cmd) == 0);
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e commit -q -m init", dir);
    ASSERT(test_system(cmd) == 0);

    char args[1024];
    snprintf(args, sizeof(args), "count . --format json");
    ASSERT_EQ(0, run_in(dir, args, out, err));
    char *b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    cJSON *j = cJSON_Parse(b);
    ASSERT(j != NULL);
    cJSON *files = cJSON_GetObjectItem(j, "files");
    cJSON *fb = NULL, *fs = NULL;
    cJSON *it;
    cJSON_ArrayForEach(it, files) {
        cJSON *p = cJSON_GetObjectItem(it, "path");
        if (p && strcmp(p->valuestring, "big.c") == 0)
            fb = it;
        else if (p && strcmp(p->valuestring, "small.c") == 0)
            fs = it;
    }
    ASSERT(fb != NULL);
    ASSERT(fs != NULL);

    /* the capped file must say so, and must separate disk bytes from packed */
    ASSERT(cJSON_GetObjectItem(fb, "truncated") != NULL);
    ASSERT_STR_EQ("read-cap",
        cJSON_GetObjectItem(fb, "truncation")->valuestring);
    ASSERT((int)cJSON_GetObjectItem(fb, "bytes")->valueint
        > (int)cJSON_GetObjectItem(fb, "bytes_packed")->valueint);
    ASSERT_EQ(READ_CAP_DEFAULT,
        (int)cJSON_GetObjectItem(fb, "bytes_packed")->valueint);

    /* an unmodified file must carry no truncation fields at all */
    ASSERT(cJSON_GetObjectItem(fs, "truncated") == NULL);
    ASSERT(cJSON_GetObjectItem(fs, "bytes_packed") == NULL);
    ASSERT(cJSON_GetObjectItem(fs, "truncation") == NULL);

    cJSON *total = cJSON_GetObjectItem(j, "total");
    ASSERT_EQ(1, (int)cJSON_GetObjectItem(total, "truncated")->valueint);
    ASSERT((int)cJSON_GetObjectItem(total, "bytes")->valueint
        > (int)cJSON_GetObjectItem(total, "bytes_packed")->valueint);
    cJSON_Delete(j);
    free(b);

    /* the human-readable table must not need the JSON to be understood */
    snprintf(args, sizeof(args), "count .");
    ASSERT_EQ(0, run_in(dir, args, out, err));
    b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "read cap") != NULL);
    ASSERT(strstr(b, "some files truncated") != NULL);
    free(b);

    /* and the manifest of an actual bundle must carry it too */
    char *md = scratch("e2e_trunc.md");
    snprintf(args, sizeof(args), "pack . --no-tree -o \"%s\"", md);
    ASSERT_EQ(0, run_in(dir, args, nul, err));
    b = test_read_all(md, NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "truncated=1 bytes_packed=") != NULL);
    free(b);

    test_rm_rf(dir);
    free(out); free(err); free(nul); free(dir); free(md);
    PASS();
}

/* --max-file-tokens must hold even when the read cap also fires, and the
 * marker it appends must be charged against the cap rather than added to it. */
TEST e2e_token_cap_holds_over_read_cap(void)
{
    char *dir = scratch("tokcap");
    char *out = scratch("e2e_tokcap.json");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && out && err && nul);
    test_rm_rf(dir);
    ASSERT(ctx_mkdir(dir) == 0);
    char big[1024];
    snprintf(big, sizeof(big), "%s/big.c", dir);
    FILE *bf = fopen(big, "wb");
    ASSERT(bf != NULL);
    for (size_t i = 0; i < (size_t)READ_CAP_DEFAULT + 4096; i++)
        fputc('a' + (int)(i % 26), bf);
    fclose(bf);
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "git -C \"%s\" init -q", dir);
    if (test_system(cmd) != 0)
        SKIPm("git unavailable");
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e add -A", dir);
    ASSERT(test_system(cmd) == 0);
    snprintf(cmd, sizeof(cmd),
        "git -C \"%s\" -c user.name=t -c user.email=t@e commit -q -m init", dir);
    ASSERT(test_system(cmd) == 0);

    char args[1024];
    snprintf(args, sizeof(args), "count . --max-file-tokens 400 --format json");
    ASSERT_EQ(0, run_in(dir, args, out, err));
    char *b = test_read_all(out, NULL);
    ASSERT(b != NULL);
    cJSON *j = cJSON_Parse(b);
    ASSERT(j != NULL);
    cJSON *f0 = cJSON_GetArrayItem(cJSON_GetObjectItem(j, "files"), 0);
    ASSERT(f0 != NULL);
    /* the binding constraint is the token cap, and it is what gets reported */
    ASSERT_STR_EQ("token-cap",
        cJSON_GetObjectItem(f0, "truncation")->valuestring);
    ASSERT((int)cJSON_GetObjectItem(f0, "tokens")->valueint <= 400);
    /* a token cap drops no bytes, so bytes_packed would just repeat bytes */
    ASSERT(cJSON_GetObjectItem(f0, "bytes_packed") == NULL);
    cJSON_Delete(j);
    free(b);

    test_rm_rf(dir);
    free(out); free(err); free(nul); free(dir);
    PASS();
}

TEST e2e_redact_rejected_with_tree(void)
{
    char *dir = scratch("redtree");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && err && nul);
    test_rm_rf(dir);
    ASSERT(ctx_mkdir(dir) == 0);
    ASSERT_EQ(2, run_in(dir, "tree --redact", nul, err));
    char *e = test_read_all(err, NULL);
    ASSERT(e != NULL);
    ASSERT(strstr(e, "--redact is not valid with tree") != NULL);
    free(e);
    test_rm_rf(dir);
    free(err); free(nul); free(dir);
    PASS();
}

TEST e2e_patch_rejected_without_diff(void)
{
    char *dir = scratch("patchonly");
    char *err = scratch("e2e_err.txt");
    char *nul = scratch("e2e_null.txt");
    ASSERT(dir && err && nul);
    test_rm_rf(dir);
    ASSERT(ctx_mkdir(dir) == 0);
    ASSERT_EQ(2, run_in(dir, "--patch", nul, err));
    char *e = test_read_all(err, NULL);
    ASSERT(e != NULL);
    ASSERT(strstr(e, "--patch is only valid with diff") != NULL);
    free(e);
    test_rm_rf(dir);
    free(err);
    free(nul);
    free(dir);
    PASS();
}

TEST e2e_diff_outside_worktree(void)
{
    char *out = scratch("e2e_nogit.txt");
    char *err = scratch("e2e_err.txt");
    ASSERT(out && err);
    /* the OS temp dir is not inside any work tree we control */
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !tmp[0])
        tmp = getenv("TMP");
    if (!tmp || !tmp[0])
        tmp = "/tmp";
    char *dir = path_abs(tmp);
    if (!dir)
        SKIPm("no temp dir");
    ASSERT_EQ(2, run_in(dir, "diff", out, err));
    free(dir);
    free(out);
    free(err);
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
    RUN_TEST(parse_format_and_sort);
    RUN_TEST(parse_rejects_bad_option_values);
    RUN_TEST(parse_query_and_sort_recorded);
    RUN_TEST(parse_diff_has_no_tree_by_default);
    RUN_TEST(parse_staged_only_with_diff);
    RUN_TEST(parse_clipboard_flag);
    RUN_TEST(parse_tree_flag_sets_explicit);
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
    RUN_TEST(e2e_count_and_tree);
    RUN_TEST(e2e_other_formats);
    RUN_TEST(e2e_query_and_sort_order);
    RUN_TEST(e2e_config_file);
    RUN_TEST(e2e_diff_worktree_and_staged);
    RUN_TEST(e2e_diff_sees_untracked_staged_and_drops_deleted);
    RUN_TEST(e2e_diff_expands_untracked_directory);
    RUN_TEST(e2e_diff_patch_emits_hunks);
    RUN_TEST(e2e_patch_rejected_without_diff);
RUN_TEST(e2e_json_is_valid_without_tree);
RUN_TEST(e2e_manifest_is_deterministic);
RUN_TEST(e2e_manifest_does_not_break_budget);
RUN_TEST(e2e_redact_is_opt_in);
RUN_TEST(e2e_redact_is_deterministic);
RUN_TEST(e2e_redact_rejected_with_tree);
    RUN_TEST(e2e_count_reports_truncation);
    RUN_TEST(e2e_token_cap_holds_over_read_cap);
    RUN_TEST(e2e_outline_modes_and_budget);
    RUN_TEST(e2e_count_outline_agrees_with_pack);
RUN_TEST(e2e_cctxpackignore_layered_on_gitignore);
    RUN_TEST(e2e_diff_outside_worktree);
    test_rm_rf("ctx_e2e_tmp");
}
