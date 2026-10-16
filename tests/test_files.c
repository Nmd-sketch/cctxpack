#include "greatest.h"

#include "files.h"
#include "glob.h"
#include "helpers.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <sys/utime.h>
static int set_mtime(const char *path, time_t t)
{
    struct _utimbuf u;
    u.actime = t;
    u.modtime = t;
    return _utime(path, &u);
}
#else
#include <utime.h>
static int set_mtime(const char *path, time_t t)
{
    struct utimbuf u;
    u.actime = t;
    u.modtime = t;
    return utime(path, &u);
}
#endif

TEST glob_star_matches_any_depth(void)
{
    ASSERT(glob_match("a.md", "*.md"));
    ASSERT(glob_match("deep/a.md", "*.md"));
    ASSERT(glob_match("a/b/c/d.md", "*.md"));
    ASSERT(!glob_match("a.txt", "*.md"));
    ASSERT(!glob_match("deep/a.mdx", "*.md"));
    PASS();
}

TEST glob_anchored_pattern(void)
{
    ASSERT(glob_match("src/main.c", "src/*.c"));
    ASSERT(!glob_match("src/sub/x.c", "src/*.c"));
    ASSERT(!glob_match("other/main.c", "src/*.c"));
    ASSERT(glob_match("src/a/b/x.c", "src/**/*.c"));
    PASS();
}

TEST glob_doublestar_prefix(void)
{
    ASSERT(glob_match("a.min.js", "**/*.min.js"));
    ASSERT(glob_match("d/a.min.js", "**/*.min.js"));
    ASSERT(!glob_match("a.min.jsx", "**/*.min.js"));
    ASSERT(glob_match("src/a.c", "src/**"));
    ASSERT(glob_match("src/a/b/c.c", "src/**"));
    ASSERT(!glob_match("srcx/a.c", "src/**"));
    PASS();
}

TEST glob_question_and_class(void)
{
    ASSERT(glob_match("a.c", "?.c"));
    ASSERT(!glob_match("ab.c", "?.c"));
    ASSERT(!glob_match("/.c", "?.c"));
    ASSERT(glob_match("b.c", "[abc].c"));
    ASSERT(!glob_match("d.c", "[abc].c"));
    ASSERT(glob_match("m.c", "[a-z].c"));
    ASSERT(glob_match("a.c", "[!x].c"));
    ASSERT(!glob_match("x.c", "[!x].c"));
    PASS();
}

TEST glob_unclosed_bracket_is_literal(void)
{
    ASSERT(glob_match("a[.c", "a[.c"));
    ASSERT(!glob_match("ab.c", "a[.c"));
    PASS();
}

TEST glob_filter_include_then_exclude(void)
{
    strlist paths, inc, exc;
    strlist_init(&paths);
    strlist_init(&inc);
    strlist_init(&exc);
    strlist_push(&paths, "a.c");
    strlist_push(&paths, "b.md");
    strlist_push(&paths, "sub/c.c");
    strlist_push(&inc, "*.c");
    glob_filter(&paths, &inc, NULL);
    ASSERT_EQ(2, (int)paths.n);
    ASSERT_STR_EQ("a.c", paths.v[0]);
    ASSERT_STR_EQ("sub/c.c", paths.v[1]);
    strlist_push(&exc, "sub/*");
    glob_filter(&paths, NULL, &exc);
    ASSERT_EQ(1, (int)paths.n);
    ASSERT_STR_EQ("a.c", paths.v[0]);
    strlist_free(&paths);
    strlist_free(&inc);
    strlist_free(&exc);
    PASS();
}

TEST glob_filter_no_include_keeps_all(void)
{
    strlist paths;
    strlist_init(&paths);
    strlist_push(&paths, "x");
    strlist_push(&paths, "y");
    glob_filter(&paths, NULL, NULL);
    ASSERT_EQ(2, (int)paths.n);
    strlist_free(&paths);
    PASS();
}

TEST root_filter_prefix_and_exact(void)
{
    strlist paths, roots;
    strlist_init(&paths);
    strlist_init(&roots);
    strlist_push(&paths, "src/a.c");
    strlist_push(&paths, "src/sub/b.c");
    strlist_push(&paths, "docs/d.md");
    strlist_push(&paths, "srcx/e.c");
    strlist_push(&roots, "src");
    root_filter(&paths, &roots);
    ASSERT_EQ(2, (int)paths.n);
    ASSERT_STR_EQ("src/a.c", paths.v[0]);
    ASSERT_STR_EQ("src/sub/b.c", paths.v[1]);
    strlist_free(&paths);
    strlist_free(&roots);

    strlist_init(&paths);
    strlist_init(&roots);
    strlist_push(&paths, "docs/d.md");
    strlist_push(&paths, "other.md");
    strlist_push(&roots, "docs/d.md");
    root_filter(&paths, &roots);
    ASSERT_EQ(1, (int)paths.n);
    ASSERT_STR_EQ("docs/d.md", paths.v[0]);
    strlist_free(&paths);
    strlist_free(&roots);
    PASS();
}

TEST strlist_push_grows(void)
{
    strlist l;
    strlist_init(&l);
    for (int i = 0; i < 100; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "item%d", i);
        ASSERT(strlist_push(&l, buf) == 0);
    }
    ASSERT_EQ(100, (int)l.n);
    ASSERT_STR_EQ("item0", l.v[0]);
    ASSERT_STR_EQ("item99", l.v[99]);
    strlist_free(&l);
    PASS();
}

TEST files_sort_byte_order(void)
{
    strlist l;
    strlist_init(&l);
    strlist_push(&l, "b.txt");
    strlist_push(&l, "a.c");
    strlist_push(&l, "A.c");
    files_sort(&l);
    ASSERT_STR_EQ("A.c", l.v[0]);
    ASSERT_STR_EQ("a.c", l.v[1]);
    ASSERT_STR_EQ("b.txt", l.v[2]);
    strlist_free(&l);
    PASS();
}

TEST files_git_uses_injected_output(void)
{
    test_rm_rf("faketmp");
    ASSERT(ctx_mkdir("faketmp") == 0);
    strlist l;
    strlist_init(&l);
    ASSERT_EQ(0, files_git("faketmp", "a.txt\nsub/b.txt\n", &l));
    ASSERT_EQ(2, (int)l.n);
    ASSERT_STR_EQ("a.txt", l.v[0]);
    ASSERT_STR_EQ("sub/b.txt", l.v[1]);
    strlist_free(&l);
    test_rm_rf("faketmp");
    PASS();
}

TEST files_walk_nested_skips_dotgit(void)
{
    test_rm_rf("walktmp");
    ASSERT(ctx_mkdir("walktmp") == 0);
    ASSERT(test_write_file("walktmp/a.txt", "a", 1) == 0);
    ASSERT(ctx_mkdir("walktmp/sub") == 0);
    ASSERT(test_write_file("walktmp/sub/b.txt", "b", 1) == 0);
    ASSERT(ctx_mkdir("walktmp/.git") == 0);
    ASSERT(test_write_file("walktmp/.git/c.txt", "c", 1) == 0);
    strlist l;
    strlist_init(&l);
    ASSERT_EQ(0, files_walk("walktmp", &l));
    files_sort(&l);
    ASSERT_EQ(2, (int)l.n);
    ASSERT_STR_EQ("a.txt", l.v[0]);
    ASSERT_STR_EQ("sub/b.txt", l.v[1]);
    strlist_free(&l);
    test_rm_rf("walktmp");
    PASS();
}

TEST files_sort_mode_size_and_mtime(void)
{
    test_rm_rf("sorttmp");
    ASSERT(ctx_mkdir("sorttmp") == 0);
    ASSERT(test_write_file("sorttmp/big.txt",
        "012345678901234567890123456789", 30) == 0);
    ASSERT(test_write_file("sorttmp/small.txt", "abc", 3) == 0);
    ASSERT(test_write_file("sorttmp/old.txt", "o", 1) == 0);
    ASSERT(test_write_file("sorttmp/new.txt", "n", 1) == 0);
    ASSERT(set_mtime("sorttmp/old.txt", (time_t)1000000000) == 0);
    ASSERT(set_mtime("sorttmp/new.txt", (time_t)1200000000) == 0);

    ASSERT_EQ(30, (int)files_sort_key("sorttmp", "big.txt", SORT_SIZE));
    ASSERT_EQ(3, (int)files_sort_key("sorttmp", "small.txt", SORT_SIZE));
    ASSERT_EQ(0, (int)files_sort_key("sorttmp", "big.txt", SORT_NAME));
    ASSERT_EQ(-1200000000LL,
        files_sort_key("sorttmp", "new.txt", SORT_MTIME));
    /* an unreadable path degrades to key 0 instead of failing the run */
    ASSERT_EQ(0, (int)files_sort_key("sorttmp", "nope.txt", SORT_SIZE));

    strlist l;
    strlist_init(&l);
    ASSERT(strlist_push(&l, "big.txt") == 0);
    ASSERT(strlist_push(&l, "small.txt") == 0);
    files_sort_mode(&l, "sorttmp", SORT_SIZE);
    ASSERT_STR_EQ("small.txt", l.v[0]);
    ASSERT_STR_EQ("big.txt", l.v[1]);

    strlist_free(&l);
    strlist_init(&l);
    ASSERT(strlist_push(&l, "old.txt") == 0);
    ASSERT(strlist_push(&l, "new.txt") == 0);
    files_sort_mode(&l, "sorttmp", SORT_MTIME);
    ASSERT_STR_EQ("new.txt", l.v[0]);
    ASSERT_STR_EQ("old.txt", l.v[1]);

    strlist_free(&l);
    strlist_init(&l);
    ASSERT(strlist_push(&l, "small.txt") == 0);
    ASSERT(strlist_push(&l, "big.txt") == 0);
    files_sort_mode(&l, "sorttmp", SORT_NAME);
    ASSERT_STR_EQ("big.txt", l.v[0]);
    ASSERT_STR_EQ("small.txt", l.v[1]);
    strlist_free(&l);
    test_rm_rf("sorttmp");
    PASS();
}

SUITE(files_suite)
{
    RUN_TEST(glob_star_matches_any_depth);
    RUN_TEST(glob_anchored_pattern);
    RUN_TEST(glob_doublestar_prefix);
    RUN_TEST(glob_question_and_class);
    RUN_TEST(glob_unclosed_bracket_is_literal);
    RUN_TEST(glob_filter_include_then_exclude);
    RUN_TEST(glob_filter_no_include_keeps_all);
    RUN_TEST(root_filter_prefix_and_exact);
    RUN_TEST(strlist_push_grows);
    RUN_TEST(files_sort_byte_order);
    RUN_TEST(files_sort_mode_size_and_mtime);
    RUN_TEST(files_git_uses_injected_output);
    RUN_TEST(files_walk_nested_skips_dotgit);
}
