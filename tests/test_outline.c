#include "greatest.h"
#include "helpers.h"
#include "outline.h"
#include "util.h"

#include <string.h>

static int build(const char *src, const char *path, outline_list *ol)
{
    return outline_build(src, strlen(src), path, ol);
}

static int has_line(outline_list *ol, size_t line, const char *needle)
{
    for (size_t i = 0; i < ol->n; i++) {
        if (ol->v[i].line == line && strstr(ol->v[i].decl, needle))
            return 1;
    }
    return 0;
}

TEST outline_family_selection(void)
{
    ASSERT(outline_family("a.c") == 0);
    ASSERT(outline_family("a.H") == 0);
    ASSERT(outline_family("x/y/z.ts") == 0);
    ASSERT(outline_family("m.py") == 1);
    ASSERT(outline_family("README.md") == -1);
    ASSERT(outline_family("data.json") == -1);
    ASSERT(outline_family("Makefile") == -1);   /* no extension */
    ASSERT(outline_family("noext") == -1);
    PASS();
}

TEST outline_c_declarations_and_imports(void)
{
    outline_list ol;
    const char *src = "#include <stdio.h>\n"
                      "#include \"local.h\"\n"
                      "\n"
                      "int add(int a, int b){\n"
                      "  return a + b;\n"
                      "}\n"
                      "\n"
                      "static void helper(void)\n"
                      "{\n"
                      "}\n";
    ASSERT(build(src, "t.c", &ol) == 0);
    ASSERT(has_line(&ol, 1, "#include <stdio.h>"));
    ASSERT(has_line(&ol, 2, "local.h"));
    ASSERT(has_line(&ol, 4, "int add"));
    /* the two-line signature must be attributed to the line it starts on */
    ASSERT(has_line(&ol, 8, "static void helper"));
    ASSERT(ol.n == 4);
    outline_free(&ol);
    PASS();
}

TEST outline_skips_braces_in_strings_and_comments(void)
{
    outline_list ol;
    const char *src = "// comment with { brace\n"
                      "/* block { comment */\n"
                      "const char *s = \"{ not a brace\";\n"
                      "int f(void){ return 1; }\n";
    ASSERT(build(src, "t.c", &ol) == 0);
    ASSERT(has_line(&ol, 4, "int f"));
    ASSERT(ol.n == 1);
    outline_free(&ol);
    PASS();
}

TEST outline_excludes_control_flow(void)
{
    outline_list ol;
    const char *src = "void f(void){\n"
                      "  if (x) { y(); }\n"
                      "  for(;;){ z(); }\n"
                      "  while(q){ r(); }\n"
                      "  int g(int a){ return a; }\n"
                      "}\n";
    ASSERT(build(src, "t.c", &ol) == 0);
    ASSERT(has_line(&ol, 1, "void f"));
    ASSERT(has_line(&ol, 5, "int g"));
    for (size_t i = 0; i < ol.n; i++)
        ASSERT(strstr(ol.v[i].decl, "if (") == NULL);
    outline_free(&ol);
    PASS();
}

TEST outline_python(void)
{
    outline_list ol;
    const char *src = "import os\n"
                      "from collections import OrderedDict\n"
                      "\n"
                      "class Foo:\n"
                      "    def method(self):\n"
                      "        return 1\n"
                      "\n"
                      "def top():\n"
                      "    pass\n";
    ASSERT(build(src, "t.py", &ol) == 0);
    ASSERT(has_line(&ol, 1, "import os"));
    ASSERT(has_line(&ol, 2, "OrderedDict"));
    ASSERT(has_line(&ol, 4, "class Foo"));
    ASSERT(has_line(&ol, 5, "def method"));
    ASSERT(has_line(&ol, 8, "def top"));
    outline_free(&ol);
    PASS();
}

TEST outline_js_imports_and_one_liner_class(void)
{
    outline_list ol;
    const char *src = "import x from 'y';\n"
                      "const a = require('z');\n"
                      "function go(a,b) {\n"
                      "  return a+b;\n"
                      "}\n"
                      "class K { m() { return 1; } }\n";
    ASSERT(build(src, "t.js", &ol) == 0);
    ASSERT(has_line(&ol, 1, "import x"));
    ASSERT(has_line(&ol, 2, "require"));
    ASSERT(has_line(&ol, 3, "function go"));
    ASSERT(has_line(&ol, 6, "class K"));
    /* one entry per line: class K { m() { ... } } opens twice */
    ASSERT(ol.n == 4);
    outline_free(&ol);
    PASS();
}

TEST outline_rust_raw_string(void)
{
    outline_list ol;
    const char *src = "use std::fmt;\n"
                      "fn main() {\n"
                      "    let s = r#\"{ raw string }\"#;\n"
                      "}\n";
    ASSERT(build(src, "t.rs", &ol) == 0);
    ASSERT(has_line(&ol, 1, "use std::fmt"));
    ASSERT(has_line(&ol, 2, "fn main"));
    outline_free(&ol);
    PASS();
}

/* The fallback is the load-bearing behaviour: a confident wrong outline is
 * worse than no outline, so anything the scanner cannot account for must be
 * reported as unparseable. */
TEST outline_refuses_unbalanced_braces(void)
{
    outline_list ol;
    ASSERT(build("int f(void){ \n  if (x) {\n", "t.c", &ol) == -1);
    ASSERT(ol.n == 0);
    outline_free(&ol);
    ASSERT(build("void f(){}\nvoid g(){ \n", "t.c", &ol) == -1);
    outline_free(&ol);
    PASS();
}

TEST outline_refuses_unknown_extension(void)
{
    outline_list ol;
    ASSERT(build("int f(void){ return 1; }\n", "notes.md", &ol) == -1);
    outline_free(&ol);
    PASS();
}

/* A header is all prototypes and includes, and those ARE the declarations
 * worth listing. */
TEST outline_prototype_only_header(void)
{
    outline_list ol;
    const char *src = "#ifndef CTX_FIT_H\n"
                      "#define CTX_FIT_H\n"
                      "#include <stddef.h>\n"
                      "\n"
                      "size_t pack_fit(const size_t *e, size_t n);\n"
                      "\n"
                      "#endif\n";
    ASSERT(build(src, "fit.h", &ol) == 0);
    ASSERT(has_line(&ol, 3, "stddef.h"));
    ASSERT(has_line(&ol, 5, "pack_fit"));
    outline_free(&ol);
    PASS();
}

/* A brace on its own line after a *statement* is a scope block, not a
 * function body.  Attributing it to the statement would list `strbuf_init(...)`
 * as a declaration, and the first version of this scanner did exactly that.
 * Regression test: it also catches a stale previous-line length, which
 * truncated the captured text. */
TEST outline_bare_brace_after_statement_is_not_a_decl(void)
{
    outline_list ol;
    const char *src = "void f(void){\n"
                      "    strbuf probe;\n"
                      "    strbuf_init(&probe);\n"
                      "    {\n"
                      "        use(&probe);\n"
                      "    }\n"
                      "}\n";
    ASSERT(build(src, "t.c", &ol) == 0);
    ASSERT(has_line(&ol, 1, "void f"));
    for (size_t i = 0; i < ol.n; i++)
        ASSERT(strstr(ol.v[i].decl, "strbuf_init") == NULL);
    outline_free(&ol);
    PASS();
}

/* A signature split across lines must be attributed to the line it STARTS on,
 * and captured whole rather than truncated mid-token - a stale previous-line
 * length produced `strbuf_init(&pro`.  Only the first line is listed, which is
 * a documented limit rather than a bug: the point is to name the declaration
 * and where it starts. */
TEST outline_two_line_signature_is_not_truncated(void)
{
    outline_list ol;
    const char *src = "static size_t pack_fit(const size_t *ests,\n"
                      "    size_t n)\n"
                      "{\n"
                      "    return 0;\n"
                      "}\n";
    ASSERT(build(src, "t.c", &ol) == 0);
    ASSERT(ol.n == 1);
    ASSERT(ol.v[0].line == 1);
    ASSERT(strcmp(ol.v[0].decl,
        "static size_t pack_fit(const size_t *ests,") == 0);
    outline_free(&ol);
    PASS();
}

TEST outline_empty_file(void)
{
    outline_list ol;
    ASSERT(build("", "t.c", &ol) == 0);
    ASSERT(ol.n == 0);
    outline_free(&ol);
    PASS();
}

SUITE(outline_suite)
{
    RUN_TEST(outline_family_selection);
    RUN_TEST(outline_c_declarations_and_imports);
    RUN_TEST(outline_skips_braces_in_strings_and_comments);
    RUN_TEST(outline_excludes_control_flow);
    RUN_TEST(outline_python);
    RUN_TEST(outline_js_imports_and_one_liner_class);
    RUN_TEST(outline_rust_raw_string);
    RUN_TEST(outline_refuses_unbalanced_braces);
    RUN_TEST(outline_refuses_unknown_extension);
    RUN_TEST(outline_prototype_only_header);
    RUN_TEST(outline_bare_brace_after_statement_is_not_a_decl);
    RUN_TEST(outline_two_line_signature_is_not_truncated);
    RUN_TEST(outline_empty_file);
}
