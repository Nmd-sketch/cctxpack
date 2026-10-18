#include "greatest.h"
#include "helpers.h"
#include "redact.h"
#include "util.h"

#include <string.h>

/* Apply redaction to `in`; caller frees `out`. */
static void run(const char *in, strbuf *out, size_t *count)
{
    strbuf_init(out);
    *count = redact_apply(in, strlen(in), out);
}

static size_t lines_of(const char *s)
{
    size_t n = 0;
    for (const char *p = s; *p; p++)
        if (*p == '\n')
            n++;
    return n;
}

/* A clean input must come back byte-identical.  This is the property that
 * makes --redact safe to turn on: it only ever changes what it matched. */
TEST redact_clean_input_untouched(void)
{
    strbuf out;
    size_t n;
    const char *src = "#include <stdio.h>\n"
                      "int main(void) { printf(\"hi\\n\"); return 0; }\n";
    run(src, &out, &n);
    ASSERT(n == 0);
    ASSERT_STR_EQ(src, out.data);
    strbuf_free(&out);
    PASS();
}

TEST redact_vendor_token_shapes(void)
{
    struct {
        const char *in;
        const char *kind;
    } cases[] = {
        { "AKIAIOSFODNN7EXAMPLE", "aws-access-key-id" },
        { "ghp_0123456789abcdefghijklmnopqrstuvwxyz", "github-token" },
        { "gho_0123456789abcdefghijklmnopqrstuvwxyz", "github-oauth" },
        { "github_pat_11ABCDEFG0abcdefghijklmnopqrs", "github-pat" },
        { "AIzaSyA1234567890abcdefghijklmnopqrstuv", "google-api-key" },
        /* Deliberately `xoxz-`, not `xoxb-`: GitHub push protection matches
         * the real Slack sub-prefixes and would block the commit as a leaked
         * credential.  `z` is not a Slack prefix, so this exercises the same
         * `xox` rule while proving the rule is not hardcoded to a-z. */
        { "xoxz-123456789012-abcdefghijklmnop", "slack-token" },
        { "sk_live_0123456789abcdefghij", "stripe-secret-key" },
        { "eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxIn0.abc", "jwt" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        strbuf out;
        size_t n;
        char in[256], want[128];
        snprintf(in, sizeof(in), "id = %s\n", cases[i].in);
        run(in, &out, &n);
        ASSERT(n == 1);
        snprintf(want, sizeof(want), "id = [REDACTED:%s]\n", cases[i].kind);
        ASSERT_STR_EQ(want, out.data);
        strbuf_free(&out);
    }
    PASS();
}

TEST redact_assignment_replaces_value_only(void)
{
    strbuf out;
    size_t n;
    const char *src = "db_password = \"s3cr3t-prod-value\"\n";
    run(src, &out, &n);
    ASSERT(n == 1);
    /* the statement must survive intact, quote delimiters included */
    ASSERT_STR_EQ("db_password = \"[REDACTED:password]\"\n", out.data);
    strbuf_free(&out);
    PASS();
}

TEST redact_assignment_single_quoted(void)
{
    strbuf out;
    size_t n;
    /* The assignment matcher fires at the NAME, so the marker names the
     * variable rather than the vendor shape that happens to be its value.
     * Both are correct output; this pins the precedence. */
    run("api_key: 'AKIAIOSFODNN7EXAMPLX'\n", &out, &n);
    ASSERT(n == 1);
    ASSERT_STR_EQ("api_key: '[REDACTED:api_key]'\n", out.data);
    strbuf_free(&out);

    /* ...but with no secret-shaped NAME in front, the vendor rule wins. */
    run("id = 'AKIAIOSFODNN7EXAMPLX'\n", &out, &n);
    ASSERT(n == 1);
    ASSERT_STR_EQ("id = '[REDACTED:aws-access-key-id]'\n", out.data);
    strbuf_free(&out);
    PASS();
}

TEST redact_skips_placeholders_and_derivations(void)
{
    const char *safe[] = {
        "password = \"your-password-here\"\n",
        "api_key = \"${API_KEY}\"\n",
        "secret = \"<your-secret>\"\n",
        "client_secret = \"changeme\"\n",
        "password_hash = \"$2y$10$abcdefghijklmnopqrstuv\"\n",
        "token = \"\"\n",
        "pin = \"1234\"\n",               /* under the 6-byte minimum */
        "keyword = \"a literal word\"\n", /* not a secret-shaped name */
        "sha = \"e3b0c44298fc1c149afbf4c8996fb924\"\n",
        "key = \"AKIA\"\n",               /* below the shape's min length */
        "comment = \"use AKIA to configure\"\n",
    };
    for (size_t i = 0; i < sizeof(safe) / sizeof(*safe); i++) {
        strbuf out;
        size_t n;
        run(safe[i], &out, &n);
        ASSERT(n == 0);
        ASSERT_STR_EQ(safe[i], out.data);
        strbuf_free(&out);
    }
    PASS();
}

TEST redact_pem_block_is_whole_and_keeps_line_count(void)
{
    strbuf out;
    size_t n;
    const char *src =
        "before\n"
        "-----BEGIN RSA PRIVATE KEY-----\n"
        "MIIEowIBAAKCAQEA0Z3VS5JJcds3xfn/ygWyF0qJ9mB4nK7pQ2rX8vT1uY6wA3d\n"
        "F5gH7jK9lM2nO4pQ6rS8tU0vW2xY4zA6bC8dE0fG2hI4jK6lM8nO0pQ2rS4tU6vW8\n"
        "-----END RSA PRIVATE KEY-----\n"
        "after\n";
    run(src, &out, &n);
    ASSERT(n == 1);
    /* neither delimiter may survive, and the trailing END line specifically:
     * a naive search for "-----" after finding "-----END " stops on the END
     * marker's own leading dashes and leaves that line exposed. */
    ASSERT(strstr(out.data, "BEGIN") == NULL);
    ASSERT(strstr(out.data, "END RSA PRIVATE KEY") == NULL);
    ASSERT(strstr(out.data, "MIIEowIBAAKC") == NULL);
    ASSERT(strstr(out.data, "[REDACTED:private-key]") != NULL);
    ASSERT(strstr(out.data, "after") != NULL);
    ASSERT(lines_of(src) == lines_of(out.data));
    strbuf_free(&out);
    PASS();
}

TEST redact_single_line_span_keeps_line_count(void)
{
    strbuf out;
    size_t n;
    const char *src = "a\nAKIAIOSFODNN7EXAMPLE\nb\n";
    run(src, &out, &n);
    ASSERT(n == 1);
    ASSERT_STR_EQ("a\n[REDACTED:aws-access-key-id]\nb\n", out.data);
    ASSERT(lines_of(src) == lines_of(out.data));
    strbuf_free(&out);
    PASS();
}

TEST redact_multiple_spans_counted(void)
{
    strbuf out;
    size_t n;
    const char *src = "AKIAIOSFODNN7EXAMPLE\n"
                      "password = \"realsecret1\"\n"
                      "nothing here\n";
    run(src, &out, &n);
    ASSERT(n == 2);
    ASSERT(strstr(out.data, "realsecret1") == NULL);
    ASSERT(lines_of(src) == lines_of(out.data));
    strbuf_free(&out);
    PASS();
}

TEST redact_preserves_order_and_everything_else(void)
{
    strbuf out;
    size_t n;
    const char *src = "line1\n"
                      "token = 'ghp_0123456789abcdefghijklmnopqrstuvwxyz'\n"
                      "line3\n"
                      "key = \"AKIAIOSFODNN7EXAMPLE\"\n"
                      "line5\n";
    run(src, &out, &n);
    ASSERT(n == 2);
    ASSERT(strstr(out.data, "line1") != NULL);
    ASSERT(strstr(out.data, "line3") != NULL);
    ASSERT(strstr(out.data, "line5") != NULL);
    ASSERT(strstr(out.data, "ghp_0123456789") == NULL);
    ASSERT(strstr(out.data, "AKIAIOSFODNN7EXAMPLE") == NULL);
    ASSERT(lines_of(src) == lines_of(out.data));
    strbuf_free(&out);
    PASS();
}

TEST redact_empty_input(void)
{
    strbuf out;
    size_t n;
    run("", &out, &n);
    ASSERT(n == 0);
    strbuf_free(&out);
    PASS();
}

/* A file that merely MENTIONS "-----BEGIN " is documentation, not a key.  An
 * earlier version treated a missing END as "redact to end of file", which
 * destroyed this tool's own source. */
TEST redact_unterminated_begin_is_not_a_key(void)
{
    strbuf out;
    size_t n;
    const char *src = "static const char begin[] = \"-----BEGIN \";\n"
                      "int main(void) { return 0; }\n";
    run(src, &out, &n);
    ASSERT(n == 0);
    ASSERT_STR_EQ(src, out.data);
    strbuf_free(&out);
    PASS();
}

/* Idempotent: re-scrubbing a redacted bundle must not mangle the markers, and
 * documentation showing this tool's own output must survive intact. */
TEST redact_existing_markers_pass_through(void)
{
    strbuf out;
    size_t n;
    const char *src = "k = \"[REDACTED:aws-access-key-id]\"\n"
                      "see [REDACTED:private-key] for details\n"
                      "new = \"AKIAIOSFODNN7EXAMPLE\"\n";
    run(src, &out, &n);
    ASSERT(n == 1);
    ASSERT_STR_EQ("k = \"[REDACTED:aws-access-key-id]\"\n"
                  "see [REDACTED:private-key] for details\n"
                  "new = \"[REDACTED:aws-access-key-id]\"\n", out.data);

    /* and a second pass is a no-op.  `out` must stay alive: it is the input. */
    strbuf second;
    size_t m;
    run(out.data, &second, &m);
    ASSERT(m == 0);
    ASSERT_STR_EQ(out.data, second.data);
    strbuf_free(&second);
    strbuf_free(&out);
    PASS();
}

SUITE(redact_suite)
{
    RUN_TEST(redact_clean_input_untouched);
    RUN_TEST(redact_vendor_token_shapes);
    RUN_TEST(redact_assignment_replaces_value_only);
    RUN_TEST(redact_assignment_single_quoted);
    RUN_TEST(redact_skips_placeholders_and_derivations);
    RUN_TEST(redact_pem_block_is_whole_and_keeps_line_count);
    RUN_TEST(redact_single_line_span_keeps_line_count);
    RUN_TEST(redact_multiple_spans_counted);
    RUN_TEST(redact_preserves_order_and_everything_else);
    RUN_TEST(redact_empty_input);
    RUN_TEST(redact_unterminated_begin_is_not_a_key);
    RUN_TEST(redact_existing_markers_pass_through);
}