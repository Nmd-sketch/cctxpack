#include "greatest.h"

#include "config.h"
#include "format.h"
#include "helpers.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

static char *probe_path(const char *name)
{
    char joined[512];
    ctx_mkdir("ctx_cfg_tmp");
    snprintf(joined, sizeof(joined), "ctx_cfg_tmp/%s", name);
    return path_abs(joined);
}

static void opts_reset(pack_opts *o)
{
    memset(o, 0, sizeof(*o));
    strlist_init(&o->path_args);
    strlist_init(&o->include);
    strlist_init(&o->exclude);
    o->format = FMT_MD;
    o->want_tree = 1;
}

static void opts_free(pack_opts *o)
{
    strlist_free(&o->path_args);
    strlist_free(&o->include);
    strlist_free(&o->exclude);
}

static int write_json(const char *path, const char *json)
{
    return test_write_file(path, json, strlen(json));
}

TEST config_read_all_keys(void)
{
    char *p = probe_path("full.json");
    ASSERT(p != NULL);
    ASSERT(write_json(p, "{\"budget\":1234,\"format\":\"xml\","
        "\"include\":[\"src/**\"],\"exclude\":[\"*.log\"],"
        "\"max_file_tokens\":99,\"tree\":false}\n") == 0);
    config c;
    config_init(&c);
    ASSERT_EQ(0, config_read(p, &c));
    ASSERT_EQ(1, c.has_budget);
    ASSERT_EQ(1234, (int)c.budget);
    ASSERT_EQ(1, c.has_max);
    ASSERT_EQ(99, (int)c.max_file_tokens);
    ASSERT_EQ(1, c.has_format);
    ASSERT_EQ(FMT_XML, c.format);
    ASSERT_EQ(1, c.has_tree);
    ASSERT_EQ(0, c.tree);
    ASSERT_EQ(1, (int)c.include.n);
    ASSERT_STR_EQ("src/**", c.include.v[0]);
    ASSERT_EQ(1, (int)c.exclude.n);
    ASSERT_STR_EQ("*.log", c.exclude.v[0]);
    config_free(&c);
    free(p);
    PASS();
}

TEST config_read_partial_keeps_absent_keys_unset(void)
{
    char *p = probe_path("partial.json");
    ASSERT(p != NULL);
    ASSERT(write_json(p, "{\"format\":\"json\"}\n") == 0);
    config c;
    config_init(&c);
    ASSERT_EQ(0, config_read(p, &c));
    ASSERT_EQ(0, c.has_budget);
    ASSERT_EQ(0, c.has_max);
    ASSERT_EQ(0, c.has_tree);
    ASSERT_EQ(1, c.has_format);
    ASSERT_EQ(FMT_JSON, c.format);
    ASSERT_EQ(0, (int)c.include.n);
    config_free(&c);
    free(p);
    PASS();
}

TEST config_read_rejects_bad_documents(void)
{
    char *p = probe_path("bad.json");
    config c;
    ASSERT(p != NULL);
    config_init(&c);

    ASSERT(write_json(p, "{oops\n") == 0);
    ASSERT_EQ(2, config_read(p, &c));
    ASSERT(write_json(p, "{\"budget\":\"lots\"}\n") == 0);
    ASSERT_EQ(2, config_read(p, &c));
    ASSERT(write_json(p, "{\"budget\":-1}\n") == 0);
    ASSERT_EQ(2, config_read(p, &c));
    ASSERT(write_json(p, "{\"format\":\"html\"}\n") == 0);
    ASSERT_EQ(2, config_read(p, &c));
    ASSERT(write_json(p, "{\"tree\":\"yes\"}\n") == 0);
    ASSERT_EQ(2, config_read(p, &c));
    ASSERT(write_json(p, "{\"exclude\":\"*.log\"}\n") == 0);
    ASSERT_EQ(2, config_read(p, &c));
    ASSERT(write_json(p, "{} {}\n") == 0);
    ASSERT_EQ(2, config_read(p, &c));
    ASSERT(write_json(p, "") == 0);
    ASSERT_EQ(2, config_read(p, &c));

    ASSERT_EQ(2, config_read("ctx_cfg_tmp/does-not-exist.json", &c));
    config_free(&c);
    free(p);
    PASS();
}

TEST config_read_empty_object_is_noop(void)
{
    char *p = probe_path("empty.json");
    ASSERT(p != NULL);
    ASSERT(write_json(p, "{}\n") == 0);
    config c;
    config_init(&c);
    ASSERT_EQ(0, config_read(p, &c));
    ASSERT_EQ(0, c.has_budget && c.has_max && c.has_tree && c.has_format);
    config_free(&c);
    free(p);
    PASS();
}

TEST config_merge_cli_wins(void)
{
    config c;
    config_init(&c);
    c.has_budget = 1;
    c.budget = 10;
    c.has_max = 1;
    c.max_file_tokens = 20;
    c.has_tree = 1;
    c.tree = 0;
    c.has_format = 1;
    c.format = FMT_XML;
    ASSERT(strlist_push(&c.include, "src/**") == 0);
    ASSERT(strlist_push(&c.exclude, "*.log") == 0);

    pack_opts o;
    opts_reset(&o);
    ASSERT_EQ(0, config_merge(&c, &o));
    ASSERT_EQ(1, o.has_budget);
    ASSERT_EQ(10, (int)o.budget);
    ASSERT_EQ(1, o.max_tokens_set);
    ASSERT_EQ(20, (int)o.max_file_tokens);
    ASSERT_EQ(0, o.want_tree);
    ASSERT_EQ(FMT_XML, o.format);
    ASSERT_EQ(1, (int)o.include.n);
    ASSERT_EQ(1, (int)o.exclude.n);
    opts_free(&o);

    /* every key the CLI set explicitly survives the merge */
    opts_reset(&o);
    o.has_budget = 1;
    o.budget = 77;
    o.max_tokens_set = 1;
    o.max_file_tokens = 78;
    o.tree_set = 1;
    o.want_tree = 1;
    o.fmt_set = 1;
    o.format = FMT_TXT;
    ASSERT(strlist_push(&o.include, "cli/**") == 0);
    ASSERT(strlist_push(&o.exclude, "cli.log") == 0);
    ASSERT_EQ(0, config_merge(&c, &o));
    ASSERT_EQ(77, (int)o.budget);
    ASSERT_EQ(78, (int)o.max_file_tokens);
    ASSERT_EQ(1, o.want_tree);
    ASSERT_EQ(FMT_TXT, o.format);
    ASSERT_EQ(1, (int)o.include.n);
    ASSERT_STR_EQ("cli/**", o.include.v[0]);
    ASSERT_EQ(1, (int)o.exclude.n);
    ASSERT_STR_EQ("cli.log", o.exclude.v[0]);
    opts_free(&o);

    config_free(&c);
    PASS();
}

TEST config_find_walks_up(void)
{
    char *dir = probe_path("finddir");
    char *sub = probe_path("finddir/sub");
    ASSERT(dir && sub);
    ctx_mkdir(dir);
    ctx_mkdir(sub);
    char cp[1024];
    snprintf(cp, sizeof(cp), "%s/.cctxpack.json", dir);
    ASSERT(write_json(cp, "{}\n") == 0);

    char *saved = path_cwd();
    ASSERT(saved != NULL);
    ASSERT(ctx_chdir(sub) == 0);
    char *found = NULL;
    int rc = config_find(&found);
    ctx_chdir(saved);
    free(saved);
    ASSERT_EQ(1, rc);
    ASSERT(found != NULL);
    ASSERT(strstr(found, "finddir") != NULL);
    ASSERT(strstr(found, ".cctxpack.json") != NULL);
    free(found);
    free(dir);
    free(sub);
    PASS();
}

TEST config_apply_end_to_end(void)
{
    char *dir = probe_path("applydir");
    ASSERT(dir != NULL);
    ctx_mkdir(dir);
    char cp[1024];
    snprintf(cp, sizeof(cp), "%s/.cctxpack.json", dir);
    ASSERT(write_json(cp, "{\"budget\":4096,\"format\":\"txt\"}\n") == 0);

    char *saved = path_cwd();
    ASSERT(saved != NULL);
    ASSERT(ctx_chdir(dir) == 0);
    pack_opts o;
    opts_reset(&o);
    int rc = config_apply(&o);
    ctx_chdir(saved);
    free(saved);
    ASSERT_EQ(0, rc);
    ASSERT_EQ(1, o.has_budget);
    ASSERT_EQ(4096, (int)o.budget);
    ASSERT_EQ(FMT_TXT, o.format);
    opts_free(&o);
    free(dir);
    PASS();
}

SUITE(config_suite)
{
    test_rm_rf("ctx_cfg_tmp");
    RUN_TEST(config_read_all_keys);
    RUN_TEST(config_read_partial_keeps_absent_keys_unset);
    RUN_TEST(config_read_rejects_bad_documents);
    RUN_TEST(config_read_empty_object_is_noop);
    RUN_TEST(config_merge_cli_wins);
    RUN_TEST(config_find_walks_up);
    RUN_TEST(config_apply_end_to_end);
    test_rm_rf("ctx_cfg_tmp");
}
