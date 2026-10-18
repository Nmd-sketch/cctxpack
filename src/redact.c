/* Opt-in credential redaction.
 *
 * The rule here is narrow on purpose.  This tool exists to hand a model the
 * exact bytes of a repository, so the one unforgivable mistake is rewriting
 * something that is not a secret - a fixture called `password`, a docs example,
 * a hash.  Every pattern below is a fixed, vendor-issued, high-entropy token
 * shape that does not occur by accident.
 *
 * Deliberately absent: entropy scoring.  Length-and-alphabet heuristics are how
 * redactors end up mangling SHA digests, base64 blobs, lockfile integrity
 * hashes and minified bundles, and they still miss most real keys because those
 * keys sit in env vars rather than in source.
 *
 * Also deliberately absent: pattern names on the right-hand side of an
 * assignment unless the name is unambiguously secret AND the value looks like a
 * real literal rather than a placeholder.
 */

#include "redact.h"

#include <stdlib.h>
#include <string.h>

/* --- character classes ------------------------------------------------- */

static int is_alnum(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9');
}

/* Extra characters allowed after a rule's literal prefix. */
static int in_tail(int c, const char *tail)
{
    if (!tail)
        return is_alnum(c);
    return is_alnum(c) || strchr(tail, c) != NULL;
}

/* --- literal-prefixed vendor token shapes ------------------------------- */

typedef struct {
    const char *id;
    const char *prefix;
    size_t min_len;       /* prefix + at least this many more bytes */
    const char *tail;     /* extra tail chars, or NULL for alnum only */
} literal_rule;

static const literal_rule RULES[] = {
    { "aws-access-key-id", "AKIA",   20, NULL    },
    { "github-token",      "ghp_",   40, NULL    },
    { "github-oauth",      "gho_",   40, NULL    },
    { "github-app",        "ghs_",   36, NULL    },
    { "github-refresh",    "ghr_",   36, NULL    },
    { "github-pat",        "github_pat_", 30, "_-" },
    { "google-api-key",    "AIza",   39, "-_"    },
    { "slack-token",       "xox",    15, "-"     },
    { "stripe-secret-key", "sk_live_", 24, NULL  },
    { "jwt",               "eyJ",    20, "._-"   },
};

/* --- assignment-shaped secrets ------------------------------------------ */

/* Key names that are secret whatever the language.  Deliberately excludes
 * `key`, `id`, `token`-free names: too many false positives in tests. */
static const char *const SECRET_NAMES[] = {
    "password", "passwd", "secret", "api_key", "apikey", "api-key",
    "access_token", "auth_token", "client_secret", "private_key",
    "aws_secret_access_key", "connection_string",
};

/* Values that are obviously not real secrets.  Skipping these is what keeps
 * `--redact` from destroying example code and test fixtures. */
static const char *const PLACEHOLDERS[] = {
    "your", "example", "changeme", "change_me", "changethis", "placeholder",
    "dummy", "fake", "sample", "test", "xxxx", "todo", "fixme", "redacted",
    "notreal", "insert", "abcdefgh", "12345678",
};

static int is_placeholder(const char *v, size_t n)
{
    for (size_t i = 0; i < sizeof(PLACEHOLDERS) / sizeof(*PLACEHOLDERS); i++) {
        const char *p = PLACEHOLDERS[i];
        size_t pl = strlen(p);
        if (n >= pl) {
            for (size_t j = 0; j + pl <= n; j++) {
                size_t k = 0;
                while (k < pl) {
                    char a = v[j + k], b = p[k];
                    if (a >= 'A' && a <= 'Z')
                        a = (char)(a + 32);
                    if (a != b)
                        break;
                    k++;
                }
                if (k == pl)
                    return 1;
            }
        }
    }
    /* templated or interpolated, not a literal secret */
    for (size_t j = 0; j < n; j++) {
        if (v[j] == '<' || v[j] == '>' || v[j] == '{' || v[j] == '}'
            || v[j] == '$')
            return 1;
    }
    return 0;
}

static int name_matches(const char *p, size_t avail, const char *name)
{
    size_t nl = strlen(name);
    if (avail < nl)
        return 0;
    for (size_t i = 0; i < nl; i++) {
        char a = p[i], b = name[i];
        if (a >= 'A' && a <= 'Z')
            a = (char)(a + 32);
        if (a != b)
            return 0;
    }
    /* must not be the tail of a longer identifier: `password` matches
     * `password`, not `db_password_hash` */
    if (avail > nl) {
        char nx = p[nl];
        if (is_alnum((unsigned char)nx) || nx == '_')
            return 0;
    }
    return 1;
}

/* Try `password = "..."` style at `p`.  On a hit returns the value span and
 * sets *id. */
static int match_assignment(const char *p, size_t avail, const char **id,
    size_t *vstart, size_t *vlen)
{
    for (size_t i = 0; i < sizeof(SECRET_NAMES) / sizeof(*SECRET_NAMES); i++) {
        const char *name = SECRET_NAMES[i];
        if (!name_matches(p, avail, name))
            continue;
        size_t j = strlen(name);
        while (j < avail && (p[j] == ' ' || p[j] == '\t'))
            j++;
        if (j >= avail || (p[j] != '=' && p[j] != ':'))
            continue;
        j++;
        while (j < avail && (p[j] == ' ' || p[j] == '\t'))
            j++;
        char quote = 0;
        if (j < avail && (p[j] == '"' || p[j] == '\'')) {
            quote = p[j];
            j++;
        }
        size_t vs = j;
        while (j < avail && p[j] != '\n' && p[j] != '\r') {
            if (quote) {
                if (p[j] == quote)
                    break;
            } else {
                if (p[j] == ' ' || p[j] == '\t' || p[j] == ';'
                    || p[j] == ',' || p[j] == ')')
                    break;
            }
            j++;
        }
        size_t vl = j - vs;
        if (quote && (j >= avail || p[j] != quote))
            continue;                   /* unterminated: not a literal */
        if (vl < 6)
            continue;
        if (is_placeholder(p + vs, vl))
            continue;
        *id = name;
        *vstart = vs;
        *vlen = vl;
        return 1;
    }
    return 0;
}

/* --- PEM private key block ---------------------------------------------- */

static size_t match_pem(const char *p, size_t avail)
{
    static const char begin[] = "-----BEGIN ";
    size_t bl = sizeof(begin) - 1;
    if (avail < bl + 14 || memcmp(p, begin, bl) != 0)
        return 0;
    const char *tail = strstr(p, "PRIVATE KEY-----");
    if (!tail || (size_t)(tail - p) + 15 > avail)
        return 0;
    const char *end = strstr(tail, "-----END ");
    /* No END: NOT a key block.  An unterminated BEGIN is far more often a
     * document that mentions one (or this tool's own source) than a truncated
     * key, and redacting "the rest of the file" on that reading destroys the
     * whole document. */
    if (!end)
        return 0;
    /* Search for the CLOSING dashes of the END marker, not the leading ones:
     * strstr(end, "-----") would match the delimiter we just found and stop
     * immediately, leaving "END RSA PRIVATE KEY-----" in the output. */
    const char *stop = strstr(end + 10, "-----");
    if (!stop || (size_t)(stop - p) + 5 > avail)
        return 0;
    return (size_t)(stop - p) + 5;
}

/* Length of an already-emitted marker at `p`, or 0.  Makes --redact
 * idempotent: re-scrubbing a redacted bundle, or packing documentation that
 * shows this tool's own output, must not mangle the markers again. */
static size_t marker_len(const char *p, size_t avail)
{
    static const char pre[] = "[REDACTED:";
    size_t pl = sizeof(pre) - 1;
    if (avail <= pl || memcmp(p, pre, pl) != 0)
        return 0;
    const char *close = memchr(p + pl, ']', avail - pl);
    if (!close)
        return 0;
    return (size_t)(close - p) + 1;
}

/* --- driver -------------------------------------------------------------- */

size_t redact_apply(const char *data, size_t len, strbuf *out)
{
    size_t count = 0, i = 0;
    while (i < len) {
        /* `span_at`/`span_len` is what gets REPLACED; `mlen` is what gets
         * CONSUMED.  They differ for assignments, where the marker must cover
         * only the value so the statement around it survives intact. */
        size_t mlen = 0, span_at = 0, span_len = 0;
        const char *id = NULL;

        mlen = match_pem(data + i, len - i);
        if (mlen) {
            id = "private-key";
            span_len = mlen;
        } else {
            for (size_t r = 0; r < sizeof(RULES) / sizeof(*RULES); r++) {
                const literal_rule *ru = &RULES[r];
                size_t pl = strlen(ru->prefix);
                if (len - i < pl || memcmp(data + i, ru->prefix, pl) != 0)
                    continue;
                size_t j = pl;
                while (j < len - i && in_tail((unsigned char)data[i + j],
                           ru->tail))
                    j++;
                if (j < ru->min_len)
                    continue;
                mlen = span_len = j;
                id = ru->id;
                break;
            }
        }
        size_t vs = 0, vl = 0;
        if (!id && match_assignment(data + i, len - i, &id, &vs, &vl)) {
            /* consume the whole `name = value`, replace only `value` */
            mlen = vs + vl;
            if (vs > 0 && (data[i + vs - 1] == '"' || data[i + vs - 1] == '\''))
                mlen -= 1;
            if (i + vs + vl < len
                && (data[i + vs + vl] == '"' || data[i + vs + vl] == '\''))
                mlen += 1;
            span_at = vs;
            span_len = vl;
        }

        if (!id) {
            size_t m = marker_len(data + i, len - i);
            if (m) {
                strbuf_appendn(out, data + i, m);
                i += m;
                continue;
            }
            strbuf_appendn(out, data + i, 1);
            i++;
            continue;
        }
        /* keep everything before the secret verbatim (the `password = "` of
         * an assignment, so the statement still parses) */
        strbuf_appendn(out, data + i, span_at);
        char marker[64];
        snprintf(marker, sizeof(marker), "[REDACTED:%s]", id);
        strbuf_append(out, marker);
        /* Preserve the line count so consumer line numbers stay valid.  The
         * marker itself lands on the line the match started on. */
        for (size_t k = 0; k < mlen; k++) {
            if (data[i + k] == '\n')
                strbuf_append(out, "\n");
        }
        i += mlen;
        count++;
    }
    return count;
}