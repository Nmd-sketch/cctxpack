#include "greatest.h"

#include "util.h"

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

SUITE(util_suite)
{
    RUN_TEST(strbuf_append_basic);
    RUN_TEST(strbuf_append_grows_past_initial_capacity);
    RUN_TEST(strbuf_appendf_formats);
    RUN_TEST(path_normalize_backslashes);
    RUN_TEST(path_normalize_mixed_and_empty);
}
