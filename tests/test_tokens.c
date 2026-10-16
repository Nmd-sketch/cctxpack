#include "greatest.h"

#include "tokens.h"

#include <string.h>

/* Goldens are ceil((10*C + 35*L) / 38), the integer form of
 * ceil((C + 3.5*L) / 3.8) with the calibrated constants from design 4.6. */

TEST tok_empty_is_zero(void)
{
    ASSERT_EQ(0, (int)tokens_estimate("", 0));
    ASSERT_EQ(0, (int)tokens_estimate_parts(0, 0));
    PASS();
}

TEST tok_parts_ceil(void)
{
    ASSERT_EQ(1, (int)tokens_estimate_parts(1, 0));   /* ceil(10/38) */
    ASSERT_EQ(1, (int)tokens_estimate_parts(3, 0));   /* ceil(30/38) */
    ASSERT_EQ(2, (int)tokens_estimate_parts(4, 0));   /* ceil(40/38) */
    ASSERT_EQ(27, (int)tokens_estimate_parts(100, 0)); /* ceil(1000/38) */
    PASS();
}

TEST tok_parts_exact_division(void)
{
    ASSERT_EQ(5, (int)tokens_estimate_parts(19, 0));   /* 190/38  */
    ASSERT_EQ(10, (int)tokens_estimate_parts(38, 0));  /* 380/38  */
    ASSERT_EQ(35, (int)tokens_estimate_parts(0, 38));  /* 1330/38 */
    ASSERT_EQ(450, (int)tokens_estimate_parts(380, 380));
    PASS();
}

TEST tok_newline_weight(void)
{
    ASSERT_EQ(1, (int)tokens_estimate_parts(0, 1));   /* ceil(35/38) */
    ASSERT_EQ(2, (int)tokens_estimate_parts(0, 2));   /* ceil(70/38) */
    ASSERT_EQ(2, (int)tokens_estimate_parts(3, 1));   /* ceil(65/38) */
    ASSERT_EQ(4, (int)tokens_estimate_parts(10, 1));  /* ceil(135/38) */
    PASS();
}

TEST tok_estimate_ascii(void)
{
    ASSERT_EQ(1, (int)tokens_estimate("a", 1));
    ASSERT_EQ(1, (int)tokens_estimate("abc", 3));
    ASSERT_EQ(2, (int)tokens_estimate("abcd", 4));
    ASSERT_EQ(3, (int)tokens_estimate("aaaaaaaaaa", 10));
    PASS();
}

TEST tok_estimate_counts_newlines(void)
{
    ASSERT_EQ(2, (int)tokens_estimate("\n", 1));
    ASSERT_EQ(2, (int)tokens_estimate("a\n", 2));
    ASSERT_EQ(5, (int)tokens_estimate("aaaaa\naaaaa\n", 12));
    PASS();
}

TEST tok_estimate_utf8_codepoints(void)
{
    /* each character is one code point regardless of UTF-8 byte length */
    ASSERT_EQ(1, (int)tokens_estimate("\xC3\xA9", 2));          /* e-acute */
    ASSERT_EQ(1, (int)tokens_estimate("\xF0\x9F\x98\x80", 4));  /* emoji */
    ASSERT_EQ(tokens_estimate_parts(2, 0),
        tokens_estimate("\xC3\xA9z", 3));
    PASS();
}

TEST tok_parts_additive(void)
{
    /* FR-007: the whole never estimates above the sum of its parts */
    const size_t c1[4] = { 19, 41, 0, 3574 };
    const size_t l1[4] = { 0, 7, 38, 181 };
    const size_t c2[4] = { 19, 3, 500, 4880 };
    const size_t l2[4] = { 0, 1, 12, 170 };
    for (int i = 0; i < 4; i++) {
        size_t whole = tokens_estimate_parts(c1[i] + c2[i], l1[i] + l2[i]);
        size_t parts = tokens_estimate_parts(c1[i], l1[i])
            + tokens_estimate_parts(c2[i], l2[i]);
        ASSERT(whole <= parts);
    }
    PASS();
}

SUITE(tokens_suite)
{
    RUN_TEST(tok_empty_is_zero);
    RUN_TEST(tok_parts_ceil);
    RUN_TEST(tok_parts_exact_division);
    RUN_TEST(tok_newline_weight);
    RUN_TEST(tok_estimate_ascii);
    RUN_TEST(tok_estimate_counts_newlines);
    RUN_TEST(tok_estimate_utf8_codepoints);
    RUN_TEST(tok_parts_additive);
}
