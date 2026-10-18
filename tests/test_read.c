#include "greatest.h"

#include "helpers.h"
#include "read.h"
#include "tokens.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

TEST read_plain_text(void)
{
    ASSERT(test_write_file("r1.txt", "hello\nworld\n", 12) == 0);
    read_file rf;
    ASSERT_EQ(0, read_file_open("r1.txt", 0, &rf));
    ASSERT(!rf.binary);
    ASSERT_EQ(12, (int)rf.len);
    ASSERT_EQ(12, (int)rf.file_size);
    ASSERT(!rf.truncated);
    ASSERT_MEM_EQ("hello\nworld\n", rf.data, 12);
    read_file_free(&rf);
    test_rm_rf("r1.txt");
    PASS();
}

TEST read_nul_in_sniff_is_binary(void)
{
    char data[100];
    memset(data, 'a', sizeof(data));
    data[10] = '\0';
    ASSERT(test_write_file("r2.bin", data, sizeof(data)) == 0);
    read_file rf;
    ASSERT_EQ(0, read_file_open("r2.bin", 0, &rf));
    ASSERT(rf.binary);
    ASSERT(rf.data == NULL);
    ASSERT_EQ(100, (int)rf.file_size);
    test_rm_rf("r2.bin");
    PASS();
}

TEST read_nul_after_sniff_is_text(void)
{
    char *data = malloc(9000);
    ASSERT(data != NULL);
    memset(data, 'a', 9000);
    data[8500] = '\0';
    ASSERT(test_write_file("r3.txt", data, 9000) == 0);
    free(data);
    read_file rf;
    ASSERT_EQ(0, read_file_open("r3.txt", 0, &rf));
    ASSERT(!rf.binary);
    ASSERT_EQ(9000, (int)rf.len);
    read_file_free(&rf);
    test_rm_rf("r3.txt");
    PASS();
}

TEST read_token_cap_truncates_with_marker(void)
{
    strbuf sb;
    strbuf_init(&sb);
    for (int i = 0; i < 200; i++)
        strbuf_append(&sb, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
    ASSERT(test_write_file("r4.txt", sb.data, sb.len) == 0);
    size_t orig_est = tokens_estimate(sb.data, sb.len);
    read_file rf;
    ASSERT_EQ(0, read_file_open("r4.txt", 100, &rf));
    ASSERT(rf.truncated);
    ASSERT(strstr(rf.data, "… [truncated at 100 est. tokens]") != NULL);
    /* the cut must land on a line boundary: the marker follows a newline */
    ASSERT(strstr(rf.data, "\n… [truncated at 100 est. tokens]") != NULL);
    ASSERT(tokens_estimate(rf.data, rf.len) < orig_est);
    ASSERT(tokens_estimate(rf.data, rf.len) <= 130);
    ASSERT(rf.len < sb.len);
    read_file_free(&rf);
    strbuf_free(&sb);
    test_rm_rf("r4.txt");
    PASS();
}

TEST read_byte_cap_truncates(void)
{
    size_t n = READ_CAP_DEFAULT + 1000;
    char *data = malloc(n);
    ASSERT(data != NULL);
    memset(data, 'x', n);
    ASSERT(test_write_file("r5.txt", data, n) == 0);
    free(data);
    read_file rf;
    ASSERT_EQ(0, read_file_open("r5.txt", 0, &rf));
    ASSERT(rf.truncated);
    ASSERT(strstr(rf.data, "byte read cap") != NULL);
    ASSERT(rf.len < n);
    read_file_free(&rf);
    test_rm_rf("r5.txt");
    PASS();
}

/* `bytes` is measured on disk while `lines`/`tokens` are measured on what was
 * read, so a read-capped file must report how much was actually packed.
 * Without `packed` the two numbers silently describe different things. */
TEST read_reports_packed_and_reason(void)
{
    size_t n = READ_CAP_DEFAULT + 4096;
    char *data = malloc(n);
    ASSERT(data != NULL);
    memset(data, 'x', n);
    ASSERT(test_write_file("r6.txt", data, n) == 0);
    free(data);
    read_file rf;
    ASSERT_EQ(0, read_file_open("r6.txt", 0, &rf));
    ASSERT(rf.truncated);
    ASSERT_EQ(TRUNC_READ_CAP, rf.trunc_reason);
    ASSERT_EQ(READ_CAP_DEFAULT, rf.packed);
    ASSERT((uint64_t)rf.packed < rf.file_size);
    /* `len` includes the marker, `packed` does not */
    ASSERT(rf.len > rf.packed);
    read_file_free(&rf);
    test_rm_rf("r6.txt");
    PASS();
}

TEST read_small_file_is_not_truncated(void)
{
    ASSERT(test_write_file("r7.txt", "short\n", 6) == 0);
    read_file rf;
    ASSERT_EQ(0, read_file_open("r7.txt", 0, &rf));
    ASSERT(!rf.truncated);
    ASSERT_EQ(TRUNC_NONE, rf.trunc_reason);
    ASSERT_EQ(rf.len, rf.packed);
    ASSERT_EQ((uint64_t)rf.len, rf.file_size);
    read_file_free(&rf);
    test_rm_rf("r7.txt");
    PASS();
}

/* A token cap must be honoured even when the read cap fired first.  This used
 * to be an `else if`, so --max-file-tokens was silently ignored for every file
 * at or above READ_CAP_DEFAULT - a 4 MB read is ~1.2M estimated tokens, so the
 * user got roughly 600x the budget they asked for.
 *
 * Doubles as the guard against the quadratic cut loop this newly exposed: the
 * old backward scan re-estimated the whole prefix once per line, which on this
 * file never completes. */
TEST read_token_cap_still_applies_over_read_cap(void)
{
    size_t n = READ_CAP_DEFAULT + 4096;
    char *data = malloc(n);
    ASSERT(data != NULL);
    for (size_t i = 0; i < n; i++)
        data[i] = (char)('a' + (i % 26));
    ASSERT(test_write_file("r8.txt", data, n) == 0);
    free(data);
    read_file rf;
    ASSERT_EQ(0, read_file_open("r8.txt", 500, &rf));
    ASSERT(rf.truncated);
    /* the token cap is the binding constraint and is what gets reported */
    ASSERT_EQ(TRUNC_TOKEN_CAP, rf.trunc_reason);
    /* the marker itself is charged against the cap, so the result fits the
     * request rather than exceeding it by the marker's own size */
    ASSERT(tokens_estimate(rf.data, rf.len) <= 500);
    read_file_free(&rf);
    test_rm_rf("r8.txt");
    PASS();
}

TEST read_trunc_reason_names(void)
{
    ASSERT(strcmp(trunc_reason_name(TRUNC_NONE), "none") == 0);
    ASSERT(strcmp(trunc_reason_name(TRUNC_READ_CAP), "read-cap") == 0);
    ASSERT(strcmp(trunc_reason_name(TRUNC_TOKEN_CAP), "token-cap") == 0);
    PASS();
}

TEST read_missing_file_fails(void)
{
    read_file rf;
    ASSERT(read_file_open("r_missing_xyz.txt", 0, &rf) != 0);
    PASS();
}

SUITE(read_suite)
{
    RUN_TEST(read_plain_text);
    RUN_TEST(read_nul_in_sniff_is_binary);
    RUN_TEST(read_nul_after_sniff_is_text);
    RUN_TEST(read_token_cap_truncates_with_marker);
    RUN_TEST(read_byte_cap_truncates);
    RUN_TEST(read_reports_packed_and_reason);
    RUN_TEST(read_small_file_is_not_truncated);
    RUN_TEST(read_token_cap_still_applies_over_read_cap);
    RUN_TEST(read_trunc_reason_names);
    RUN_TEST(read_missing_file_fails);
}
