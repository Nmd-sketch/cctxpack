/* Heuristic outline extraction.
 *
 * The goal is not a correct parse.  It is a cheap, mostly-right list of what a
 * file declares, with line numbers, so that a whole repository fits in a
 * fraction of the context window.  Everything here is a heuristic, and the
 * design leans on that: when the scanner is not confident it says so
 * (outline_build returns -1) and the caller emits the full text instead.
 *
 * Strings and comments are skipped while scanning, because a single '{' inside
 * a string literal or a shell comment otherwise desynchronises the whole file.
 * Brace depth must return to zero by EOF or the file is rejected.
 *
 * Declarations are recognised at the moment a '{' opens rather than at the end
 * of the line.  That matters in two directions: `int f(void) { ... }` on one
 * line never has depth > 0 when the line ends, and a signature split across
 * two lines has its '{' on a line containing nothing but the brace.
 */

#include "outline.h"

#include "util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int decl_like(const char *s, int n);
static int prototype_outline(const char *d, size_t len, outline_list *out,
    int got);

/* --- extension families -------------------------------------------------- */

/* Extensions we claim.  A path outside these gets -1, i.e. full text: better
 * to send a Markdown file verbatim than to invent structure for it. */
static const char *const BRACE_EXT[] = {
    "c", "h", "cc", "cpp", "cxx", "hpp", "hxx", "m", "mm",
    "cs", "java", "js", "jsx", "mjs", "cjs", "ts", "tsx",
    "go", "rs", "swift", "kt", "kts", "scala", "php", "d", "zig", NULL
};
static const char *const INDENT_EXT[] = {
    "py", "pyi", "pyw", NULL
};

static int has_ext(const char *path, const char *const *exts)
{
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    if (!dot || (slash && dot < slash) || dot[1] == '\0')
        return 0;
    for (const char *const *e = exts; *e; e++) {
        size_t n = strlen(*e);
        if (strlen(dot + 1) != n)
            continue;
        size_t i = 0;
        while (i < n) {
            char a = dot[1 + i], b = (*e)[i];
            if (a >= 'A' && a <= 'Z')
                a = (char)(a + 32);
            if (a != b)
                break;
            i++;
        }
        if (i == n)
            return 1;
    }
    return 0;
}

int outline_family(const char *path)
{
    if (has_ext(path, INDENT_EXT))
        return 1;
    if (has_ext(path, BRACE_EXT))
        return 0;
    return -1;
}

/* --- list ---------------------------------------------------------------- */

static int push(outline_list *l, size_t line, const char *decl, size_t n)
{
    if (l->n == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 32;
        outline_item *v = realloc(l->v, cap * sizeof(*v));
        if (!v)
            return -1;
        l->v = v;
        l->cap = cap;
    }
    char *d = malloc(n + 1);
    if (!d)
        return -1;
    memcpy(d, decl, n);
    d[n] = '\0';
    l->v[l->n].line = line;
    l->v[l->n].decl = d;
    l->n++;
    return 0;
}

void outline_free(outline_list *l)
{
    for (size_t i = 0; i < l->n; i++)
        free(l->v[i].decl);
    free(l->v);
    l->v = NULL;
    l->n = l->cap = 0;
}

/* --- text helpers -------------------------------------------------------- */

/* First identifier-shaped word, lowercased.  Stops at any character that
 * cannot appear in a keyword, so `for(;;)` yields "for" and not "for(;;){". */
static int first_word(const char *s, int n, char *buf, size_t cap)
{
    int i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t'))
        i++;
    size_t k = 0;
    while (i < n && (isalnum((unsigned char)s[i]) || s[i] == '_')) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (k + 1 < cap)
            buf[k++] = c;
        i++;
    }
    buf[k] = '\0';
    return (int)k;
}

/* Control-flow openers that also end in "{" but are not declarations. */
static const char *const NOT_DECL[] = {
    "if", "for", "while", "switch", "catch", "do", "else", "foreach",
    "with", "using", "lock", "try", "finally", "synchronized", "unless",
    "loop", "return", "case", "default", "goto", "break", "continue",
    NULL
};

/* Type/namespace openers worth listing even without parentheses. */
static const char *const TYPE_DECL[] = {
    "class", "struct", "interface", "enum", "union", "trait", "impl",
    "module", "namespace", "package", "typedef", "actor", "object",
    "record", "fn", "func", "sub", "def", "export", "public", "private",
    "protected", "internal", "abstract", "final", "static", "extern",
    "inline", "virtual", "override", "async", "operator", "template",
    "type", "const", "let", "var", "new", "this", NULL
};

static int in_list(const char *w, const char *const *list)
{
    for (const char *const *p = list; *p; p++)
        if (strcmp(w, *p) == 0)
            return 1;
    return 0;
}

static int rtrim_len(const char *s, int n)
{
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
        n--;
    return n;
}

static const char *ltrim(const char *s, int *n)
{
    while (*n > 0 && (s[0] == ' ' || s[0] == '\t')) {
        s++;
        (*n)--;
    }
    return s;
}

static int starts_with(const char *s, int n, const char *pfx)
{
    size_t k = strlen(pfx);
    return (size_t)n >= k && memcmp(s, pfx, k) == 0;
}

static int contains(const char *s, int n, char ch)
{
    for (int i = 0; i < n; i++)
        if (s[i] == ch)
            return 1;
    return 0;
}

static int find_sub(const char *hay, int n, const char *needle)
{
    size_t k = strlen(needle);
    if (k == 0 || (size_t)n < k)
        return 0;
    for (int i = 0; i + (int)k <= n; i++)
        if (memcmp(hay + i, needle, k) == 0)
            return 1;
    return 0;
}

static int is_import(const char *s, int n)
{
    if (starts_with(s, n, "#include") || starts_with(s, n, "#import"))
        return 1;
    if (starts_with(s, n, "import ") || starts_with(s, n, "import(")
        || starts_with(s, n, "from "))
        return 1;
    if (starts_with(s, n, "use ") || starts_with(s, n, "using "))
        return 1;
    if (starts_with(s, n, "require(") || starts_with(s, n, "require ("))
        return 1;
    /* `const a = require('x')` is an import in substance even though the line
     * starts with a declaration. */
    if (find_sub(s, n, "require("))
        return 1;
    if (starts_with(s, n, "export ") && contains(s, n, '"')
        && find_sub(s, n, " from "))
        return 1;
    return 0;
}

/* Called with the text of a candidate declaration line (already trimmed, and
 * ending at the '{' that opens its body). */
static int decl_like(const char *s, int n)
{
    char w[24];
    if (n <= 0)
        return 0;
    if (first_word(s, n, w, sizeof(w)) == 0)
        return 0;
    if (in_list(w, NOT_DECL))
        return 0;
    if (in_list(w, TYPE_DECL))
        return 1;
    for (int i = 0; i < n; i++)
        if (s[i] == '(')
            return 1;
    return 0;
}

/* --- brace-language scanner ---------------------------------------------- */

/* How many lines back a brace may look for its declaration, to cover a
 * parameter list split across several lines. */
#define OUTLINE_LOOKBACK 4

enum lex {
    LEX_NORMAL = 0,
    LEX_LINE_COMMENT,
    LEX_BLOCK_COMMENT,
    LEX_STRING,
    LEX_CHAR
};

static int brace_outline(const char *d, size_t len, outline_list *out)
{
    int depth = 0;
    int seen_open = 0;
    int got_decl = 0;
    int in_import_block = 0;
    enum lex lex = LEX_NORMAL;

    size_t line_no = 1;          /* line the cursor is on */
    size_t cur_start = 0;        /* byte offset of that line's first char */
    size_t i = 0;

    while (i < len) {
        char c = d[i];

        if (lex == LEX_LINE_COMMENT) {
            if (c == '\n') {
                lex = LEX_NORMAL;
                cur_start = i + 1;
                line_no++;
            }
            i++;
            continue;
        }
        if (lex == LEX_BLOCK_COMMENT) {
            if (c == '*' && i + 1 < len && d[i + 1] == '/') {
                lex = LEX_NORMAL;
                i += 2;
                continue;
            }
            if (c == '\n') {
                int ln = rtrim_len(d + cur_start, (int)(i - cur_start));
                if (ln > 0) {
                }
                cur_start = i + 1;
                line_no++;
            }
            i++;
            continue;
        }
        if (lex == LEX_STRING || lex == LEX_CHAR) {
            if (c == '\\') {
                i += 2;
                continue;
            }
            if ((lex == LEX_STRING && c == '"')
                || (lex == LEX_CHAR && c == '\''))
                lex = LEX_NORMAL;
            if (c == '\n') {
                int ln = rtrim_len(d + cur_start, (int)(i - cur_start));
                if (ln > 0) {
                }
                cur_start = i + 1;
                line_no++;
            }
            i++;
            continue;
        }

        /* LEX_NORMAL */
        if (c == '\n') {
            int ln = rtrim_len(d + cur_start, (int)(i - cur_start));
            const char *ts = ltrim(d + cur_start, &ln);
            int want = 0;
            if (ln > 0) {
                if (in_import_block && contains(ts, ln, ')')) {
                    in_import_block = 0;
                } else if (in_import_block) {
                    want = 1;
                } else if (is_import(ts, ln)) {
                    want = 1;
                    if (starts_with(ts, ln, "import") && contains(ts, ln, '(')
                        && !contains(ts, ln, ')'))
                        in_import_block = 1;
                }
                if (want && got_decl < 4000) {
                    if (push(out, line_no, ts, (size_t)ln) != 0)
                        return -1;
                    got_decl++;
                }
            }
            cur_start = i + 1;
            line_no++;
            i++;
            continue;
        }
        if (c == '/' && i + 1 < len) {
            if (d[i + 1] == '/') {
                lex = LEX_LINE_COMMENT;
                i += 2;
                continue;
            }
            if (d[i + 1] == '*') {
                lex = LEX_BLOCK_COMMENT;
                i += 2;
                continue;
            }
        }
        if (c == '"') {
            lex = LEX_STRING;
            i++;
            continue;
        }
        if (c == '\'') {
            lex = LEX_CHAR;
            i++;
            continue;
        }
        if (c == '`') {            /* Rust raw string */
            size_t j = i + 1, hashes = 0;
            while (j < len && d[j] == '#') {
                hashes++;
                j++;
            }
            if (j >= len || d[j] != '`') {
                i++;
                continue;
            }
            j++;
            while (j < len) {
                if (d[j] == '`' && j + hashes < len) {
                    size_t k = 0;
                    while (k < hashes && d[j + 1 + k] == '#')
                        k++;
                    if (k == hashes) {
                        j += 1 + hashes;
                        break;
                    }
                }
                if (d[j] == '\n') {
                    line_no++;
                    cur_start = j + 1;
                }
                j++;
            }
            i = j;
            continue;
        }
        if (c == '{') {
            /* The declaration is the text on this line up to the brace, or -
             * when the line holds nothing but the brace - the previous one. */
            int ln = rtrim_len(d + cur_start, (int)(i - cur_start));
            const char *ts = ltrim(d + cur_start, &ln);
            size_t tl = line_no;
            if (ln == 0 && i > 0) {
                /* The brace sits on a line of its own, so the declaration is
                 * on an earlier line.  Walk back up to OUTLINE_LOOKBACK lines,
                 * stopping at the first line that carries a `;`: a statement
                 * is a hard boundary (attributing a scope block to
                 * `strbuf_init(&probe);` is a false declaration), whereas a
                 * parameter list split across lines is not.
                 *
                 * Both ends of each line are taken from the buffer and bounded
                 * by `len`, because a cached previous-line length goes stale
                 * across the comment and string paths and silently truncated
                 * the text. */
                size_t at = cur_start;
                int found = 0;
                for (int back = 0; back < OUTLINE_LOOKBACK && at > 0 && !found;
                        back++) {
                    at--;
                    if (at > 0 && d[at - 1] == '\r')
                        at--;
                    size_t beg = at;
                    while (beg > 0 && d[beg - 1] != '\n')
                        beg--;
                    size_t end = at;
                    while (end < len && d[end] != '\n')
                        end++;
                    int cl = rtrim_len(d + beg, (int)(end - beg));
                    const char *cs = ltrim(d + beg, &cl);
                    if (cl == 0) {
                        at = beg;             /* blank: keep walking back */
                        continue;
                    }
                    if (contains(cs, cl, ';'))
                        break;                  /* statement: stop here */
                    if (decl_like(cs, cl)) {
                        ln = cl;
                        ts = cs;
                        tl = line_no > (size_t)back + 1
                            ? line_no - (size_t)back - 1 : 1;
                        found = 1;
                        break;
                    }
                    at = beg;
                }
            }
            /* One entry per line: `class K { m() { ... } }` opens twice and
             * would otherwise be listed twice at the same line. */
            if (!in_import_block && decl_like(ts, ln) && got_decl < 4000
                && (out->n == 0 || out->v[out->n - 1].line != tl)) {
                if (push(out, tl, ts, (size_t)ln) != 0)
                    return -1;
                got_decl++;
            }
            if (depth == 0)
                seen_open = 1;
            depth++;
            i++;
            continue;
        }
        if (c == '}') {
            depth--;
            if (depth < 0)
                return -1;
            i++;
            continue;
        }
        i++;
    }

    if (depth != 0)
        return -1;               /* unbalanced: the scan lost sync */
    if (!seen_open)
        return prototype_outline(d, len, out, got_decl);
    if (got_decl == 0 && len > 2048)
        return -1;
    return 0;
}

/* --- indent-language scanner (Python) ----------------------------------- */

static int indent_outline(const char *d, size_t len, outline_list *out)
{
    size_t i = 0, cur_start = 0, line_no = 1;
    int got = 0;
    int in_triple = 0;
    char triple_ch = 0;

    while (i <= len) {
        if (i < len && d[i] != '\n') {
            if (in_triple) {
                if (d[i] == '\\') {
                    i += 2;
                    continue;
                }
                if (d[i] == triple_ch && i + 2 < len && d[i + 1] == triple_ch
                    && d[i + 2] == triple_ch) {
                    in_triple = 0;
                    i += 3;
                    continue;
                }
            } else if ((d[i] == '"' || d[i] == '\'') && i + 2 < len
                       && d[i + 1] == d[i] && d[i + 2] == d[i]) {
                in_triple = 1;
                triple_ch = d[i];
                i += 3;
                continue;
            } else if (d[i] == '#') {
                while (i < len && d[i] != '\n')
                    i++;
                continue;
            }
            i++;
            continue;
        }
        int ln = rtrim_len(d + cur_start, (int)(i - cur_start));
        const char *ts = ltrim(d + cur_start, &ln);
        if (!in_triple && ln > 0) {
            int want = is_import(ts, ln);
            if (!want) {
                char w[24];
                first_word(ts, ln, w, sizeof(w));
                if (strcmp(w, "def") == 0 || strcmp(w, "class") == 0
                    || strcmp(w, "async") == 0 || strcmp(w, "@") == 0)
                    want = 1;
            }
            if (want && got < 4000) {
                if (push(out, line_no, ts, (size_t)ln) != 0)
                    return -1;
                got++;
            }
        }
        if (i == len)
            break;
        cur_start = i + 1;
        line_no++;
        i++;
    }
    if (got == 0 && len > 2048)
        return -1;
    return 0;
}

/* A file with no braces at all is a header of prototypes.  Those ARE the
 * declarations worth listing, and headers are a large share of a typical
 * repository's file count, so they get their own pass rather than being
 * rejected.  A header with neither prototypes nor includes is data, not code. */
static int prototype_outline(const char *d, size_t len, outline_list *out,
    int got)
{
    size_t i = 0, cur_start = 0, line_no = 1;
    int n_items = got;
    while (i <= len) {
        if (i < len && d[i] != '\n') {
            i++;
            continue;
        }
        int ln = rtrim_len(d + cur_start, (int)(i - cur_start));
        const char *ts = ltrim(d + cur_start, &ln);
        if (ln > 0) {
            int want = is_import(ts, ln)
                || (ts[ln - 1] == ';' && contains(ts, ln, '('));
            /* a prototype continued from the previous line (trailing comma) */
            if (!want && ln > 3 && ts[0] == '*')
                want = contains(ts, ln, '(') || ts[ln - 1] == ',';
            if (want && n_items < 4000) {
                if (push(out, line_no, ts, (size_t)ln) != 0)
                    return -1;
                n_items++;
            }
        }
        if (i == len)
            break;
        cur_start = i + 1;
        line_no++;
        i++;
    }
    if (n_items == 0 && len > 2048)
        return -1;
    return 0;
}

int outline_build(const char *data, size_t len, const char *path,
    outline_list *out)
{
    memset(out, 0, sizeof(*out));
    int fam = outline_family(path);
    if (fam < 0)
        return -1;
    if (len == 0)
        return 0;
    int rc = (fam == 1) ? indent_outline(data, len, out)
                        : brace_outline(data, len, out);
    if (rc != 0) {
        /* Leave no partial result behind: the contract is that -1 means an
         * EMPTY list, and a caller that only checks the return value would
         * otherwise render half an outline. */
        outline_free(out);
        memset(out, 0, sizeof(*out));
    }
    return rc;
}
