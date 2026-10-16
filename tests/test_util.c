#include "greatest.h"

#include "util.h"

#include <stdlib.h>

TEST strbuf_append_basic(void)
{
    strbuf sb;
    strbuf_init(&sb);
    ASSERT(strbuf_append(&sb, "hello") == 0);
    ASSERT_STR_EQ("hello", sb.data);
    ASSERT_EQ(5, (int)sb.len);
    ASSERT(strbuf_append(&sb, " world") == 0);
    ASSERT_STR_EQ("hello world", sb.data);
    ASSERT_EQ(11, (int)sb.len);
    strbuf_free(&sb);
    PASS();
}

TEST strbuf_append_grows_past_initial_capacity(void)
{
    strbuf sb;
    strbuf_init(&sb);
    for (int i = 0; i < 100; i++) {
        ASSERT(strbuf_append(&sb, "0123456789") == 0);
    }
    ASSERT_EQ(1000, (int)sb.len);
    ASSERT_EQ(0, (int)sb.data[1000]);
    strbuf_free(&sb);
    PASS();
}

TEST strbuf_appendf_formats(void)
{
    strbuf sb;
    strbuf_init(&sb);
    ASSERT(strbuf_appendf(&sb, "%s=%d", "n", 42) == 0);
    ASSERT_STR_EQ("n=42", sb.data);
    ASSERT(strbuf_appendf(&sb, "!") == 0);
    ASSERT_STR_EQ("n=42!", sb.data);
    strbuf_free(&sb);
    PASS();
}

TEST path_normalize_backslashes(void)
{
    char buf[] = "src\\main\\file.c";
    path_normalize(buf);
    ASSERT_STR_EQ("src/main/file.c", buf);
    PASS();
}

TEST path_normalize_mixed_and_empty(void)
{
    char buf[] = "a/b\\c";
    path_normalize(buf);
    ASSERT_STR_EQ("a/b/c", buf);
    char empty[] = "";
    path_normalize(empty);
    ASSERT_STR_EQ("", empty);
    PASS();
}

TEST path_normalize_lexical(void)
{
    static const struct { const char *in; const char *want; } cases[] = {
        { "a/./b", "a/b" },
        { "a//b", "a/b" },
        { "a/././b//c", "a/b/c" },
        { "./a", "a" },
        { "./", "." },
        { ".", "." },
        { "", "" },
        { "a/", "a" },
        { "a/b/..", "a" },
        { "a/../b", "b" },
        { "../a", "../a" },
        { "/a/b/..", "/a" },
        { "/a/../..", "/" },
        { "/", "/" },
        { "src\\main.c", "src/main.c" },
        { ".hidden/x", ".hidden/x" },
    };
    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", cases[k].in);
        path_normalize(buf);
        ASSERT_STR_EQ(cases[k].want, buf);
    }
    PASS();
}

TEST path_abs_collapses_dot_segments(void)
{
    /* Regression: the overwrite guard compares absolute paths, so "./x" and
       "x" must resolve identically on every platform. */
    char *a = path_abs("./cctxpack_norm_probe");
    char *b = path_abs("cctxpack_norm_probe");
    ASSERT(a != NULL && b != NULL);
    ASSERT_STR_EQ(b, a);
    free(a);
    free(b);
    PASS();
}

SUITE(util_suite)
{
    RUN_TEST(strbuf_append_basic);
    RUN_TEST(strbuf_append_grows_past_initial_capacity);
    RUN_TEST(strbuf_appendf_formats);
    RUN_TEST(path_normalize_backslashes);
    RUN_TEST(path_normalize_mixed_and_empty);
    RUN_TEST(path_normalize_lexical);
    RUN_TEST(path_abs_collapses_dot_segments);
}
