#include "greatest.h"

#include "cJSON.h"
#include "fit.h"
#include "format.h"
#include "helpers.h"
#include "pack.h"
#include "tokens.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

TEST fit_greedy_within_remaining(void)
{
    size_t ests[] = { 10, 20, 30, 40 };
    unsigned char incl[4];
    size_t n = pack_fit(ests, 4, 65, incl);
    ASSERT_EQ(3, (int)n);
    size_t sum = 0;
    for (size_t i = 0; i < 4; i++) {
        if (incl[i])
            sum += ests[i];
    }
    ASSERT(sum <= 65);
    PASS();
}

TEST fit_nothing_fits(void)
{
    size_t ests[] = { 5 };
    unsigned char incl[1];
    ASSERT_EQ(0, (int)pack_fit(ests, 1, 4, incl));
    ASSERT_EQ(1, (int)pack_fit(ests, 1, 5, incl));
    PASS();
}

static unsigned long xr_state = 12345;

static unsigned long xr(void)
{
    xr_state ^= xr_state << 13;
    xr_state ^= xr_state >> 7;
    xr_state ^= xr_state << 17;
    return xr_state;
}

TEST fit_random_invariant(void)
{
    size_t ests[32];
    unsigned char incl[32];
    for (unsigned long seed = 1; seed < 40; seed++) {
        xr_state = seed;
        for (size_t i = 0; i < 32; i++)
            ests[i] = xr() % 500;
        size_t budget = xr() % 5000;
        size_t n = pack_fit(ests, 32, budget, incl);
        size_t sum = 0;
        for (size_t i = 0; i < 32; i++) {
            if (incl[i])
                sum += ests[i];
        }
        ASSERT(sum <= budget);
        if (n == 0)
            ASSERT_EQ(0, (int)sum);
    }
    PASS();
}

static void pack_defaults(pack_opts *o)
{
    memset(o, 0, sizeof(*o));
    strlist_init(&o->path_args);
    strlist_init(&o->include);
    strlist_init(&o->exclude);
    o->want_tree = 1;
}

static int make_packtmp(void)
{
    test_rm_rf("ptmp");
    if (ctx_mkdir("ptmp") != 0)
        return -1;
    strbuf sb;
    strbuf_init(&sb);
    for (int i = 0; i < 50; i++)
        strbuf_append(&sb, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");
    int rc = test_write_file("ptmp/a.txt", sb.data, sb.len);
    strbuf_free(&sb);
    strbuf_init(&sb);
    for (int i = 0; i < 10; i++)
        strbuf_append(&sb, "# Heading\n\ntext body\n");
    if (rc == 0)
        rc = test_write_file("ptmp/b.md", sb.data, sb.len);
    strbuf_free(&sb);
    if (rc == 0)
        rc = test_write_file("ptmp/c.c", "int main(void) { return 0; }\n", 29);
    return rc;
}

TEST pack_run_budget_invariant(void)
{
    if (make_packtmp() != 0)
        FAILm("cannot create ptmp");
    pack_opts o;
    pack_defaults(&o);
    o.root = "ptmp";
    o.root_display = "ptmp";
    o.fake_ls = "a.txt\nb.md\nc.c\n";
    o.has_budget = 1;
    o.budget = 1000;
    o.output = "packout.md";
    test_rm_rf("packout.md");
    int rc = pack_run(&o);
    ASSERT_EQ(0, rc);
    size_t len = 0;
    char *bundle = test_read_all("packout.md", &len);
    ASSERT(bundle != NULL);
    ASSERT(tokens_estimate(bundle, len) <= o.budget);
    ASSERT(strstr(bundle, "### a.txt") != NULL);
    ASSERT(strstr(bundle, "```text") != NULL);
    free(bundle);
    test_rm_rf("packout.md");
    test_rm_rf("ptmp");
    PASS();
}

TEST pack_run_tight_budget_fits_or_exits(void)
{
    if (make_packtmp() != 0)
        FAILm("cannot create ptmp");
    pack_opts o;
    pack_defaults(&o);
    o.root = "ptmp";
    o.root_display = "ptmp";
    o.fake_ls = "a.txt\nb.md\nc.c\n";
    o.has_budget = 1;
    o.budget = 200;
    o.output = "packout2.md";
    test_rm_rf("packout2.md");
    int rc = pack_run(&o);
    if (rc == 0) {
        size_t len = 0;
        char *bundle = test_read_all("packout2.md", &len);
        ASSERT(bundle != NULL);
        ASSERT(tokens_estimate(bundle, len) <= 200);
        free(bundle);
    } else {
        ASSERT_EQ(3, rc);
    }
    test_rm_rf("packout2.md");
    test_rm_rf("ptmp");
    PASS();
}

TEST pack_run_nothing_fits_exits_3(void)
{
    if (make_packtmp() != 0)
        FAILm("cannot create ptmp");
    pack_opts o;
    pack_defaults(&o);
    o.root = "ptmp";
    o.root_display = "ptmp";
    o.fake_ls = "a.txt\nb.md\nc.c\n";
    o.has_budget = 1;
    o.budget = 1;
    o.output = "packout3.md";
    test_rm_rf("packout3.md");
    ASSERT_EQ(3, pack_run(&o));
    FILE *f = fopen("packout3.md", "rb");
    ASSERT(f == NULL);
    test_rm_rf("ptmp");
    PASS();
}

TEST pack_run_no_budget_packs_all(void)
{
    if (make_packtmp() != 0)
        FAILm("cannot create ptmp");
    pack_opts o;
    pack_defaults(&o);
    o.root = "ptmp";
    o.root_display = "ptmp";
    o.fake_ls = "a.txt\nb.md\nc.c\n";
    o.output = "packout4.md";
    test_rm_rf("packout4.md");
    ASSERT_EQ(0, pack_run(&o));
    size_t len = 0;
    char *bundle = test_read_all("packout4.md", &len);
    ASSERT(bundle != NULL);
    ASSERT(strstr(bundle, "### a.txt") != NULL);
    ASSERT(strstr(bundle, "### b.md") != NULL);
    ASSERT(strstr(bundle, "### c.c") != NULL);
    free(bundle);
    test_rm_rf("packout4.md");
    test_rm_rf("ptmp");
    PASS();
}

TEST pack_run_exclude_filter(void)
{
    if (make_packtmp() != 0)
        FAILm("cannot create ptmp");
    pack_opts o;
    pack_defaults(&o);
    o.root = "ptmp";
    o.root_display = "ptmp";
    o.fake_ls = "a.txt\nb.md\nc.c\n";
    strlist_push(&o.exclude, "*.md");
    o.output = "packout5.md";
    test_rm_rf("packout5.md");
    ASSERT_EQ(0, pack_run(&o));
    size_t len = 0;
    char *bundle = test_read_all("packout5.md", &len);
    ASSERT(bundle != NULL);
    ASSERT(strstr(bundle, "### b.md") == NULL);
    ASSERT(strstr(bundle, "### a.txt") != NULL);
    free(bundle);
    test_rm_rf("packout5.md");
    test_rm_rf("ptmp");
    PASS();
}

TEST pack_score_basics(void)
{
    ASSERT_EQ(0, (int)pack_score("src/main.c", ""));
    ASSERT_EQ(0, (int)pack_score("src/main.c", "   "));
    ASSERT_EQ(0, (int)pack_score("src/main.c", "zzz"));
    /* a basename hit counts twice: two path hits + one basename hit */
    ASSERT_EQ(3, (int)pack_score("main/main.c", "main"));
    ASSERT_EQ(2, (int)pack_score("src/main.c", "MAIN"));
    ASSERT_EQ(2, (int)pack_score("docs/guide.md", "guide"));
    ASSERT_EQ(1, (int)pack_score("docs/guide.md", "docs"));
    ASSERT_EQ(3, (int)pack_score("docs/guide.md", "docs guide"));
    /* occurrences are counted non-overlapping, path and basename separately */
    ASSERT_EQ(4, (int)pack_score("d/aaaa", "aa"));
    PASS();
}

TEST pack_run_query_orders_hits_first(void)
{
    if (make_packtmp() != 0)
        FAILm("cannot create ptmp");
    pack_opts o;
    pack_defaults(&o);
    o.root = "ptmp";
    o.root_display = "ptmp";
    o.fake_ls = "a.txt\nb.md\nc.c\n";
    o.query = "b";
    o.output = "packq.md";
    test_rm_rf("packq.md");
    ASSERT_EQ(0, pack_run(&o));
    char *b = test_read_all("packq.md", NULL);
    ASSERT(b != NULL);
    char *first = strstr(b, "### ");
    ASSERT(first != NULL);
    ASSERT(strncmp(first, "### b.md", 8) == 0);
    free(b);
    test_rm_rf("packq.md");
    test_rm_rf("ptmp");
    PASS();
}

TEST pack_run_sort_size_smallest_first(void)
{
    if (make_packtmp() != 0)
        FAILm("cannot create ptmp");
    pack_opts o;
    pack_defaults(&o);
    o.root = "ptmp";
    o.root_display = "ptmp";
    o.fake_ls = "a.txt\nb.md\nc.c\n";
    o.sort = SORT_SIZE;
    o.output = "packs.md";
    test_rm_rf("packs.md");
    ASSERT_EQ(0, pack_run(&o));
    char *b = test_read_all("packs.md", NULL);
    ASSERT(b != NULL);
    char *c = strstr(b, "### c.c");
    char *m = strstr(b, "### b.md");
    char *a = strstr(b, "### a.txt");
    ASSERT(c != NULL && m != NULL && a != NULL);
    ASSERT(c < m);
    ASSERT(m < a);
    free(b);
    test_rm_rf("packs.md");
    test_rm_rf("ptmp");
    PASS();
}

TEST pack_run_json_tokens_are_content_only(void)
{
    if (make_packtmp() != 0)
        FAILm("cannot create ptmp");
    pack_opts o;
    pack_defaults(&o);
    o.root = "ptmp";
    o.root_display = "ptmp";
    o.fake_ls = "a.txt\nb.md\nc.c\n";
    o.format = FMT_JSON;
    o.output = "packj.json";
    test_rm_rf("packj.json");
    ASSERT_EQ(0, pack_run(&o));
    char *b = test_read_all("packj.json", NULL);
    ASSERT(b != NULL);
    cJSON *j = cJSON_Parse(b);
    ASSERT(j != NULL);
    cJSON *files = cJSON_GetObjectItem(j, "files");
    ASSERT(files != NULL);
    ASSERT_EQ(3, cJSON_GetArraySize(files));
    for (int i = 0; i < 3; i++) {
        cJSON *f = cJSON_GetArrayItem(files, i);
        const char *content = cJSON_GetObjectItem(f, "content")->valuestring;
        ASSERT(content != NULL);
        ASSERT_EQ((int)tokens_estimate(content, strlen(content)),
            cJSON_GetObjectItem(f, "tokens")->valueint);
    }
    /* the same content-only numbers appear in the markdown bundle */
    cJSON_Delete(j);
    free(b);
    o.format = FMT_MD;
    o.output = "packj.md";
    test_rm_rf("packj.md");
    ASSERT_EQ(0, pack_run(&o));
    b = test_read_all("packj.md", NULL);
    ASSERT(b != NULL);
    ASSERT(strstr(b, "### a.txt") != NULL);
    free(b);
    test_rm_rf("packj.json");
    test_rm_rf("packj.md");
    test_rm_rf("ptmp");
    PASS();
}

SUITE(pack_suite)
{
    RUN_TEST(fit_greedy_within_remaining);
    RUN_TEST(fit_nothing_fits);
    RUN_TEST(fit_random_invariant);
    RUN_TEST(pack_score_basics);
    RUN_TEST(pack_run_budget_invariant);
    RUN_TEST(pack_run_tight_budget_fits_or_exits);
    RUN_TEST(pack_run_nothing_fits_exits_3);
    RUN_TEST(pack_run_no_budget_packs_all);
    RUN_TEST(pack_run_exclude_filter);
    RUN_TEST(pack_run_query_orders_hits_first);
    RUN_TEST(pack_run_sort_size_smallest_first);
    RUN_TEST(pack_run_json_tokens_are_content_only);
}
