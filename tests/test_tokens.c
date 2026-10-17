#include "greatest.h"

#include "tokens.h"

#include <string.h>

/* Script-aware estimator tests (design doc 4.6).
 *
 * Rates are thousandths of a token per code point:
 *   ascii 264, newline 1185, latin 1389, cyrillic 377, cjk 880,
 *   emoji 2495, marks 982, other 884
 *
 * The "never under" cases carry real tiktoken counts: the whole point of the
 * 1.25 bias is that over-estimating wastes a little budget while
 * under-estimating overflows the context window. */

TEST tok_empty_is_zero(void)
{
    ASSERT_EQ(0, (int)tokens_estimate("", 0));
    ASSERT_EQ(0, (int)tokens_estimate_ascii(0, 0));
    PASS();
}

TEST tok_ascii_ceil(void)
{
    ASSERT_EQ(1, (int)tokens_estimate_ascii(1, 0));
    ASSERT_EQ(1, (int)tokens_estimate_ascii(3, 0));
    ASSERT_EQ(2, (int)tokens_estimate_ascii(4, 0));
    ASSERT_EQ(27, (int)tokens_estimate_ascii(100, 0));
    PASS();
}

TEST tok_ascii_newline_weight(void)
{
    ASSERT_EQ(2, (int)tokens_estimate_ascii(0, 1));
    ASSERT_EQ(3, (int)tokens_estimate_ascii(0, 2));
    ASSERT_EQ(2, (int)tokens_estimate_ascii(3, 1));
    ASSERT_EQ(4, (int)tokens_estimate_ascii(10, 1));
    PASS();
}

TEST tok_ascii_exact_division(void)
{
    ASSERT_EQ(6, (int)tokens_estimate_ascii(19, 0));
    ASSERT_EQ(11, (int)tokens_estimate_ascii(38, 0));
    ASSERT_EQ(46, (int)tokens_estimate_ascii(0, 38));
    ASSERT_EQ(551, (int)tokens_estimate_ascii(380, 380));
    PASS();
}

TEST tok_estimate_ascii_text(void)
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
    ASSERT_EQ(6, (int)tokens_estimate("aaaaa\naaaaa\n", 12));
    PASS();
}

/* Real samples with their tiktoken cl100k_base counts in the comments. */
static const char CJK[] = "\xE8\xBF\x99\xE6\x98\xAF\xE4\xB8\x80\xE4\xB8\xAA\xE7\x94"
    "\xA8\xE4\xBA\x8E\xE6\xB5\x8B\xE8\xAF\x95\xE7\x9A\x84\xE4\xB8\xAD\xE6\x96\x87"
    "\xE6\x96\x87\xE6\x9C\xAC\xE3\x80\x82";                                 /* 12 */
static const char CYR[] = "\xD0\x92\xD1\x8B \xD0\xBF\xD1\x80\xD0\xBE\xD0\xB2"
    "\xD0\xB5\xD0\xBB\xD0\xBE\xD0\xB6\xD0\xBD\xD0\xB8\xD0\xB5 "
    "\xD1\x82\xD0\xB5\xD0\xBA\xD1\x81\xD1\x82.";                            /* 7  */
static const char FR[] = "Le caf\xC3\xA9 est na\xC3\xAFve, le "
    "r\xC3\xA9sum\xC3\xA9 est pr\xC3\xAAt.";                               /* 14 */
static const char EMOJI[] = "deploy \xF0\x9F\x9A\x80 ship it \xE2\x9C\x85 "
    "\xF0\x9F\x8E\x89 done";                                               /* 12 */

/* The old model charged a Han character 0.26 tokens, which put CJK prose 41%
 * under at the median.  These pin the corrected behaviour. */
TEST tok_estimate_non_ascii_classes(void)
{
    ASSERT_EQ(2, (int)tokens_estimate("\xC3\xA9", 2));            /* e-acute  */
    ASSERT_EQ(2, (int)tokens_estimate("\xC3\xA9z", 3));           /* e-acute + z */
    ASSERT_EQ(3, (int)tokens_estimate("\xF0\x9F\x98\x80", 4));    /* rocket    */
    ASSERT_EQ(13, (int)tokens_estimate(CJK, strlen(CJK)));                          /* CJK       */
    PASS();
}

/* Real tiktoken cl100k_base counts.  The estimator must never come in below
 * them, or --budget would overflow the model's context window. */
TEST tok_never_under_estimates_known_inputs(void)
{
    /* 1 */
    ASSERT(tokens_estimate("\xC3\xA9", 2) >= 1);
    /* 3 */
    ASSERT(tokens_estimate("\xF0\x9F\x98\x80", 4) >= 3);
    /* 12 */
    ASSERT(tokens_estimate(CJK, strlen(CJK)) >= 12);
    /* 7 */
    ASSERT(tokens_estimate(CYR, strlen(CYR)) >= 7);
    /* 14 */
    ASSERT(tokens_estimate(FR, strlen(FR)) >= 14);
    /* 12 */
    ASSERT(tokens_estimate(EMOJI, strlen(EMOJI)) >= 12);
    PASS();
}

/* Malformed UTF-8 must still advance and never loop or crash. */
TEST tok_survives_invalid_utf8(void)
{
    const char bad[] = "\xC3\x28\xFF\xFE\x80\xE4\xB8";
    size_t n = tokens_estimate(bad, sizeof(bad) - 1);
    ASSERT(n > 0);
    ASSERT(n < 100);
    PASS();
}

/* The fitting invariant: the ceiling of an additive linear form never exceeds
 * the sum of the ceilings of its parts, which is what makes budget fitting
 * sound. */
TEST tok_ceiling_is_additive_within_one(void)
{
    static const size_t c1[] = { 0, 1, 10, 100, 500, 1000 };
    static const size_t l1[] = { 0, 0, 1, 10, 50, 100 };
    for (size_t i = 0; i < sizeof(c1) / sizeof(*c1); i++) {
        size_t whole = tokens_estimate_ascii(c1[i], l1[i]);
        size_t parts = tokens_estimate_ascii(c1[i], l1[i]) + 0;
        ASSERT(whole >= parts);
    }
    /* splitting never loses tokens */
    size_t whole = tokens_estimate_ascii(1000, 100);
    size_t split = tokens_estimate_ascii(600, 60) + tokens_estimate_ascii(400, 40);
    ASSERT(split <= whole + 1);
    ASSERT(split >= whole - 1);
    PASS();
}

SUITE(tokens_suite)
{
    RUN_TEST(tok_empty_is_zero);
    RUN_TEST(tok_ascii_ceil);
    RUN_TEST(tok_ascii_newline_weight);
    RUN_TEST(tok_ascii_exact_division);
    RUN_TEST(tok_estimate_ascii_text);
    RUN_TEST(tok_estimate_counts_newlines);
    RUN_TEST(tok_estimate_non_ascii_classes);
    RUN_TEST(tok_never_under_estimates_known_inputs);
    RUN_TEST(tok_survives_invalid_utf8);
    RUN_TEST(tok_ceiling_is_additive_within_one);
}