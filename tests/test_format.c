#include "greatest.h"

#include "cJSON.h"
#include "format.h"
#include "util.h"

#include <string.h>

TEST format_parse_names(void)
{
    ASSERT_EQ(FMT_MD, format_parse("md"));
    ASSERT_EQ(FMT_XML, format_parse("xml"));
    ASSERT_EQ(FMT_JSON, format_parse("json"));
    ASSERT_EQ(FMT_TXT, format_parse("txt"));
    ASSERT_EQ(-1, format_parse("html"));
    ASSERT_STR_EQ("md", format_name(FMT_MD));
    ASSERT_STR_EQ("xml", format_name(FMT_XML));
    ASSERT_STR_EQ("json", format_name(FMT_JSON));
    ASSERT_STR_EQ("txt", format_name(FMT_TXT));
    PASS();
}

TEST format_tree_golden(void)
{
    char *paths[] = { "README.md", "docs/a.md", "docs/sub/b.md" };
    strbuf sb;
    strbuf_init(&sb);
    format_tree(&sb, paths, 3, FMT_MD);
    ASSERT_STR_EQ("```text\n"
                  "+-- README.md\n"
                  "+-- docs/\n"
                  "|  +-- a.md\n"
                  "|  +-- sub/\n"
                  "|     +-- b.md\n"
                  "```\n\n",
        sb.data);
    strbuf_free(&sb);
    PASS();
}

TEST format_tree_single_file(void)
{
    char *paths[] = { "a/b/c.txt" };
    strbuf sb;
    strbuf_init(&sb);
    format_tree(&sb, paths, 1, FMT_MD);
    ASSERT_STR_EQ("```text\n+-- a/\n   +-- b/\n      +-- c.txt\n```\n\n",
        sb.data);
    strbuf_free(&sb);
    PASS();
}

TEST format_tree_plain_and_empty(void)
{
    char *paths[] = { "a/b.txt" };
    strbuf sb;
    strbuf_init(&sb);
    format_tree(&sb, paths, 1, FMT_TXT);
    ASSERT_STR_EQ("+-- a/\n   +-- b.txt\n\n", sb.data);
    strbuf_free(&sb);

    /* md/txt render nothing for an empty list; JSON always emits an array. */
    strbuf_init(&sb);
    format_tree(&sb, NULL, 0, FMT_MD);
    ASSERT_EQ(0, (int)sb.len);
    strbuf_free(&sb);
    strbuf_init(&sb);
    format_tree(&sb, NULL, 0, FMT_JSON);
    ASSERT_STR_EQ("[]", sb.data);
    strbuf_free(&sb);
    PASS();
}

TEST format_tree_json_array(void)
{
    char *paths[] = { "a", "b/c" };
    strbuf sb;
    strbuf_init(&sb);
    format_tree(&sb, paths, 2, FMT_JSON);
    cJSON *j = cJSON_Parse(sb.data);
    ASSERT(j != NULL);
    ASSERT_EQ(2, cJSON_GetArraySize(j));
    ASSERT_STR_EQ("a", cJSON_GetArrayItem(j, 0)->valuestring);
    ASSERT_STR_EQ("b/c", cJSON_GetArrayItem(j, 1)->valuestring);
    cJSON_Delete(j);
    strbuf_free(&sb);
    PASS();
}

TEST format_open_root_and_version(void)
{
    strbuf sb;
    strbuf_init(&sb);
    format_open(&sb, "proj", FMT_MD);
    ASSERT(strncmp(sb.data, "# Context bundle: proj\n", 23) == 0);
    ASSERT(strstr(sb.data, CTX_VERSION) != NULL);
    strbuf_free(&sb);

    strbuf_init(&sb);
    format_open(&sb, "proj", FMT_TXT);
    ASSERT(strncmp(sb.data, "cctxpack bundle: proj (cctxpack ", 32) == 0);
    strbuf_free(&sb);

    strbuf_init(&sb);
    format_open(&sb, "proj", FMT_XML);
    ASSERT_STR_EQ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                  "<context root=\"proj\" version=\"" CTX_VERSION "\">\n",
        sb.data);
    strbuf_free(&sb);

    strbuf_init(&sb);
    format_open(&sb, "proj", FMT_JSON);
    ASSERT_STR_EQ("{\"root\":\"proj\",\"version\":\"" CTX_VERSION
                  "\",\"tree\":",
        sb.data);
    strbuf_free(&sb);
    PASS();
}

TEST format_section_language_hint(void)
{
    strbuf sb;
    strbuf_init(&sb);
    format_section(&sb, FMT_MD, 1, "src/main.c",
        "int main(void) { return 0; }\n", 29, 0);
    ASSERT_STR_EQ("### src/main.c\n"
                  "```c\n"
                  "int main(void) { return 0; }\n"
                  "```\n\n",
        sb.data);
    strbuf_free(&sb);
    PASS();
}

TEST format_section_extends_fence(void)
{
    strbuf sb;
    strbuf_init(&sb);
    format_section(&sb, FMT_MD, 1, "doc.md", "before\n```\ncode\n```\n", 20, 0);
    ASSERT(strstr(sb.data, "````\n") != NULL);
    ASSERT(strncmp(sb.data, "### doc.md\n````markdown\n", 24) == 0);
    strbuf_free(&sb);
    PASS();
}

TEST format_section_adds_trailing_newline(void)
{
    strbuf sb;
    strbuf_init(&sb);
    format_section(&sb, FMT_MD, 1, "x.txt", "no-newline", 10, 0);
    ASSERT_STR_EQ("### x.txt\n```text\nno-newline\n```\n\n", sb.data);
    strbuf_free(&sb);
    PASS();
}

TEST format_section_txt_separator(void)
{
    strbuf sb;
    strbuf_init(&sb);
    format_section(&sb, FMT_TXT, 1, "a.c", "int x;\n", 7, 0);
    ASSERT_STR_EQ("===== a.c =====\nint x;\n", sb.data);
    strbuf_free(&sb);

    strbuf_init(&sb);
    format_section(&sb, FMT_TXT, 0, "b.c", "y\n", 2, 0);
    ASSERT_STR_EQ("\n===== b.c =====\ny\n", sb.data);
    strbuf_free(&sb);
    PASS();
}

TEST format_section_xml_escapes(void)
{
    strbuf sb;
    strbuf_init(&sb);
    format_section(&sb, FMT_XML, 1, "a&b<c.txt", "x < y & \"z\"\n", 12, 4);
    ASSERT_STR_EQ("<file path=\"a&amp;b&lt;c.txt\" tokens=\"4\">\n"
                  "x &lt; y &amp; &quot;z&quot;\n"
                  "</file>\n",
        sb.data);
    strbuf_free(&sb);

    /* a missing final newline becomes an entity so the element stays tidy */
    strbuf_init(&sb);
    format_section(&sb, FMT_XML, 1, "a", "no-nl", 5, 0);
    ASSERT_STR_EQ("<file path=\"a\" tokens=\"0\">\nno-nl&#10;</file>\n",
        sb.data);
    strbuf_free(&sb);
    /* XML needs a blank line between entries to stay readable */
    strbuf_init(&sb);
    format_section(&sb, FMT_XML, 0, "b", "y\n", 2, 0);
    ASSERT_STR_EQ("\n<file path=\"b\" tokens=\"0\">\ny\n</file>\n", sb.data);
    strbuf_free(&sb);
    PASS();
}

TEST format_binary_placeholder(void)
{
    strbuf sb;
    strbuf_init(&sb);
    format_binary(&sb, FMT_MD, 1, "data/x.bin", 7);
    ASSERT_STR_EQ("### data/x.bin\n`«binary: data/x.bin (7 bytes)»`\n\n",
        sb.data);
    strbuf_free(&sb);

    strbuf_init(&sb);
    format_binary(&sb, FMT_XML, 1, "b.bin", 7);
    ASSERT_STR_EQ("<file path=\"b.bin\" binary=\"true\" bytes=\"7\"/>\n",
        sb.data);
    strbuf_free(&sb);

    strbuf_init(&sb);
    format_binary(&sb, FMT_TXT, 0, "b.bin", 7);
    ASSERT_STR_EQ("\n===== b.bin =====\n[binary: b.bin (7 bytes)]\n", sb.data);
    strbuf_free(&sb);
    PASS();
}

TEST format_json_document_roundtrip(void)
{
    strbuf sb;
    strbuf_init(&sb);
    char *paths[] = { "a.c", "b.bin" };
    format_open(&sb, "proj", FMT_JSON);
    format_tree(&sb, paths, 2, FMT_JSON);
    format_files_open(&sb, FMT_JSON);
    format_section(&sb, FMT_JSON, 1, "a.c", "int x;\n", 7, 10);
    format_binary(&sb, FMT_JSON, 0, "b.bin", 7);
    format_close(&sb, FMT_JSON);

    cJSON *j = cJSON_Parse(sb.data);
    ASSERT(j != NULL);
    ASSERT_STR_EQ("proj", cJSON_GetObjectItem(j, "root")->valuestring);
    ASSERT_EQ(2, cJSON_GetArraySize(cJSON_GetObjectItem(j, "tree")));
    cJSON *files = cJSON_GetObjectItem(j, "files");
    ASSERT_EQ(2, cJSON_GetArraySize(files));
    cJSON *f0 = cJSON_GetArrayItem(files, 0);
    ASSERT_STR_EQ("a.c", cJSON_GetObjectItem(f0, "path")->valuestring);
    ASSERT_EQ(10, cJSON_GetObjectItem(f0, "tokens")->valueint);
    ASSERT_STR_EQ("int x;\n", cJSON_GetObjectItem(f0, "content")->valuestring);
    cJSON *f1 = cJSON_GetArrayItem(files, 1);
    ASSERT_STR_EQ("b.bin", cJSON_GetObjectItem(f1, "path")->valuestring);
    ASSERT(cJSON_IsTrue(cJSON_GetObjectItem(f1, "binary")));
    ASSERT_EQ(7, cJSON_GetObjectItem(f1, "bytes")->valueint);
    cJSON_Delete(j);

    /* Empty file list still yields a well-formed document. */
    strbuf_free(&sb);
    strbuf_init(&sb);
    format_open(&sb, "p", FMT_JSON);
    format_tree(&sb, NULL, 0, FMT_JSON);
    format_files_open(&sb, FMT_JSON);
    format_close(&sb, FMT_JSON);
    j = cJSON_Parse(sb.data);
    ASSERT(j != NULL);
    ASSERT_EQ(0, cJSON_GetArraySize(cJSON_GetObjectItem(j, "files")));
    cJSON_Delete(j);
    strbuf_free(&sb);
    PASS();
}

TEST format_section_json_escapes_path(void)
{
    strbuf sb;
    strbuf_init(&sb);
    format_section(&sb, FMT_JSON, 1, "a\"b.c", "x\n", 2, 1);
    cJSON *j = cJSON_Parse(sb.data);
    ASSERT(j != NULL);
    ASSERT_STR_EQ("a\"b.c", cJSON_GetObjectItem(j, "path")->valuestring);
    cJSON_Delete(j);
    strbuf_free(&sb);
    PASS();
}

SUITE(format_suite)
{
    RUN_TEST(format_parse_names);
    RUN_TEST(format_tree_golden);
    RUN_TEST(format_tree_single_file);
    RUN_TEST(format_tree_plain_and_empty);
    RUN_TEST(format_tree_json_array);
    RUN_TEST(format_open_root_and_version);
    RUN_TEST(format_section_language_hint);
    RUN_TEST(format_section_extends_fence);
    RUN_TEST(format_section_adds_trailing_newline);
    RUN_TEST(format_section_txt_separator);
    RUN_TEST(format_section_xml_escapes);
    RUN_TEST(format_section_json_escapes_path);
    RUN_TEST(format_binary_placeholder);
    RUN_TEST(format_json_document_roundtrip);
}
