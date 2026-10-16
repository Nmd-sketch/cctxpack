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
    RUN_TEST(read_missing_file_fails);
}
