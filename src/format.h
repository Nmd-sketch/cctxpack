#ifndef CTX_FORMAT_H
#define CTX_FORMAT_H

#include "outline.h"
#include "util.h"

#include <stddef.h>

typedef enum {
    FMT_MD = 0,
    FMT_XML,
    FMT_JSON,
    FMT_TXT
} format_id;

const char *format_name(int fmt);
/* Returns a format_id, or -1 for an unknown name. */
int format_parse(const char *s);

/* Provenance for one bundle, enough for a consumer to say exactly what it
 * received.  Every field is derived from the input - never from wall-clock time -
 * so identical inputs still produce byte-identical output. */
typedef struct {
    const char *commit;        /* short SHA, or NULL outside a work tree */
    size_t files;              /* files included */
    size_t bytes;              /* bytes on disk of those files */
    size_t tokens;             /* estimated content tokens of those files */
    unsigned long long digest; /* FNV-1a-64 over each path + NUL, in order */
    size_t redactions;          /* 0 when --redact was off or found nothing */
    /* Files whose content did not arrive whole.  `bytes` is measured on disk
     * while `tokens` is measured on what was actually read, so the two only
     * describe the same thing when nothing was dropped - `bytes_packed` makes
     * that explicit. */
    size_t truncated;           /* 0 when nothing was truncated */
    size_t bytes_packed;        /* 0 means "same as `bytes`" */
} format_manifest;

void format_open(strbuf *b, const char *root_display, int fmt,
    const format_manifest *man);
/* Renders the directory tree.  `paths` must be in byte-wise order.  For JSON
 * the tree is an array of path strings; other formats render ASCII connectors;
 * md wraps them in a fence.  A zero-length list emits "[]" for JSON and
 * nothing otherwise. */
void format_tree(strbuf *b, char *const *paths, size_t n, int fmt);
void format_files_open(strbuf *b, int fmt);
/* `first` omits the separator JSON needs before every file but the first. */
void format_section(strbuf *b, int fmt, int first, const char *path,
    const char *content, size_t len, size_t tokens);
void format_binary(strbuf *b, int fmt, int first, const char *path,
    size_t size);
void format_close(strbuf *b, int fmt);

/* Render a declaration outline in place of a file's content.  `first` has the
 * same meaning as in format_section (the JSON array separator).  For JSON the
 * outline is a STRUCTURED array of {line, decl} rather than text, because that
 * is the machine path and a consumer can select from an array but not from
 * prose. */
void format_outline(strbuf *b, int fmt, int first, const char *path,
    const outline_list *ol, size_t tokens);

#endif
