#include "pack.h"

#include "format.h"
#include "glob.h"
#include "read.h"
#include "tokens.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define path_eq(a, b) (_stricmp((a), (b)) == 0)
#else
#define path_eq(a, b) (strcmp((a), (b)) == 0)
#endif

typedef struct {
    const char *path;
    size_t est;
    size_t tokens;
    uint64_t size;
    long long key;
    long long key2;
    int binary;
    int truncated;
    int included;
} pack_item;

size_t pack_fit(const size_t *ests, size_t n, size_t remaining,
    unsigned char *included)
{
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        if (ests[i] <= remaining) {
            included[i] = 1;
            remaining -= ests[i];
            count++;
        } else {
            included[i] = 0;
        }
    }
    return count;
}

static int cmp_item(const void *a, const void *b)
{
    const pack_item *x = a;
    const pack_item *y = b;
    if (x->key != y->key)
        return x->key < y->key ? -1 : 1;
    if (x->key2 != y->key2)
        return x->key2 < y->key2 ? -1 : 1;
    return strcmp(x->path, y->path);
}

static void join_path(char *buf, size_t cap, const char *root, const char *rel)
{
    snprintf(buf, cap, "%s/%s", root, rel);
}

static void report(const pack_opts *o, const pack_item *items, size_t n,
    size_t included, size_t truncated, size_t used)
{
    fprintf(stderr, "cctxpack: included %zu files", included);
    if (truncated)
        fprintf(stderr, " (%zu truncated)", truncated);
    fprintf(stderr, ", excluded %zu\n", n - included);
    for (size_t i = 0; i < n; i++) {
        if (!items[i].included)
            fprintf(stderr, "cctxpack: excluded %s (%s)\n", items[i].path,
                items[i].binary ? "binary" : "over budget");
    }
    if (o->has_budget)
        fprintf(stderr, "cctxpack: est. tokens %zu/%zu (budget)\n", used,
            o->budget);
    else
        fprintf(stderr, "cctxpack: est. tokens %zu\n", used);
}

/* One sink for the three destination kinds: stdout, -o file, clipboard buffer.
 * `out == NULL` means the clipboard buffer (`clip`) is being filled. */
static int sink_write(FILE *out, strbuf *clip, const char *data, size_t len)
{
    if (out)
        return fwrite(data, 1, len, out) == len ? 0 : -1;
    return strbuf_appendn(clip, data, len);
}

#ifdef _WIN32
static FILE *clip_open(void)
{
    return _popen("clip", "wb");
}

static int clip_finish(FILE *p)
{
    return _pclose(p);
}
#else
static FILE *clip_open(void)
{
    return NULL;
}

static int clip_finish(FILE *p)
{
    (void)p;
    return -1;
}
#endif

/* Flush the clipboard buffer and close an output file.  Shared by pack_run and
 * pack_patch so the two destinations cannot drift apart.  Returns 0 on
 * success. */
static int finish_out(const pack_opts *o, FILE *out, strbuf *clip)
{
    int rc = 0;
    if (o->clipboard) {
        size_t ulen = 0;
        char *u = utf8_to_utf16le(clip->len ? clip->data : "", clip->len, &ulen);
        FILE *p = u ? clip_open() : NULL;
        if (!p) {
            fprintf(stderr, "cctxpack: cannot open the clipboard helper\n");
            rc = 1;
        } else if (fwrite(u, 1, ulen, p) != ulen || clip_finish(p) != 0) {
            fprintf(stderr, "cctxpack: clipboard helper failed\n");
            rc = 1;
        }
        free(u);
    }
    if (out && out != stdout && fclose(out) != 0)
        rc = 1;
    return rc;
}

/* Sum of the estimates the bundle's own content would carry, plus a digest over
 * the included paths in emission order.  Deliberately excludes the manifest
 * text, which is rendered after the fit and accounted for separately. */
static void manifest_of(const pack_opts *o, const pack_item *items, size_t n,
    size_t count, format_manifest *man)
{
    unsigned long long h = FNV1A64_INIT;
    size_t bytes = 0, tokens = 0;
    for (size_t i = 0; i < n; i++) {
        if (!items[i].included)
            continue;
        bytes += (size_t)items[i].size;
        tokens += items[i].tokens;
        h = fnv1a64(h, items[i].path, strlen(items[i].path) + 1);
    }
    (void)o;
    man->commit = o->commit;
    man->files = count;
    man->bytes = bytes;
    man->tokens = tokens;
    man->digest = h;
}

static size_t est_of(const strbuf *b)
{
    return b->len ? tokens_estimate(b->data, b->len) : 0;
}

static int collect(const pack_opts *o, strlist *paths, pack_item **out)
{
    if (files_discover(o->root, o->fake_ls, paths, NULL) != 0)
        return -1;
    root_filter(paths, &o->path_args);
    glob_filter(paths, &o->include, &o->exclude);
    files_apply_ignore(o->root, paths);
    files_sort(paths);

    pack_item *items = calloc(paths->n ? paths->n : 1, sizeof(*items));
    if (!items)
        return -1;

    /* FR-016/FR-015: emission order is decided before any file is read, so the
     * estimate table and the budget fit stay aligned with what is rendered. */
    for (size_t i = 0; i < paths->n; i++) {
        items[i].path = paths->v[i];
        items[i].key = files_sort_key(o->root, paths->v[i], o->sort);
        if (o->query) {
            items[i].key2 = items[i].key;
            items[i].key = -(long long)pack_score(paths->v[i], o->query);
        }
    }
    if (paths->n)
        qsort(items, paths->n, sizeof(*items), cmp_item);

    for (size_t i = 0; i < paths->n; i++) {
        char full[4096];
        join_path(full, sizeof(full), o->root, items[i].path);
        read_file rf;
        if (read_file_open(full, o->max_file_tokens, &rf) != 0) {
            fprintf(stderr, "cctxpack: cannot read %s\n", items[i].path);
            free(items);
            return -1;
        }
        items[i].binary = rf.binary;
        items[i].truncated = rf.truncated;
        items[i].size = rf.file_size;
        strbuf sec;
        strbuf_init(&sec);
        if (rf.binary) {
            format_binary(&sec, o->format, 0, items[i].path,
                (size_t)rf.file_size);
        } else {
            /* FR-007: the per-file token count is the estimate of the content
             * alone, rendered with first=0 so it never depends on position. */
            items[i].tokens = tokens_estimate(rf.data, rf.len);
            format_section(&sec, o->format, 0, items[i].path, rf.data, rf.len,
                items[i].tokens);
        }
        items[i].est = est_of(&sec);
        strbuf_free(&sec);
        read_file_free(&rf);
    }
    *out = items;
    return 0;
}

/* --patch: emit the unified diff as a single bundle section.
 *
 * The diff text is passed through verbatim instead of being split back into
 * per-file sections.  That keeps this to one git invocation with no path
 * parsing at all -- git quotes paths containing unusual bytes, and mapping
 * those back onto our own path list is exactly where this kind of parser
 * breaks.  It also means the section is a valid patch that can be saved and
 * applied.  Untracked files have no diff representation, so they appear only in
 * plain `cctxpack diff`. */
static int pack_patch(const pack_opts *o)
{
    char *text = NULL, *top = NULL;
    size_t len = 0;
    int rc = files_git_patch(o->staged, &text, &len, &top);
    if (rc == FILES_NOGIT) {
        fprintf(stderr, "cctxpack: diff needs a git work tree\n");
        return 2;
    }
    if (rc != 0)
        return 1;

    int out_rc = 0;
    strbuf b, clip;
    strbuf_init(&b);
    strbuf_init(&clip);
    if (len == 0) {
        fprintf(stderr, "cctxpack: no changes%s\n",
            o->staged ? " staged" : "");
        goto done;
    }
    {
        size_t tokens = tokens_estimate(text, len);
        format_manifest man;
        man.commit = o->commit;
        man.files = 1;
        man.bytes = len;
        man.tokens = tokens;
        man.digest = fnv1a64(FNV1A64_INIT, "changes.diff", sizeof("changes.diff"));
        strbuf sec;
        strbuf_init(&sec);
        format_open(&b, "changes", o->format, &man);
        format_tree(&b, NULL, 0, o->format);   /* closes the JSON tree key */
        format_files_open(&sec, o->format);
        format_section(&sec, o->format, 1, "changes.diff", text, len, tokens);
        format_close(&sec, o->format);
        strbuf_appendn(&b, sec.data, sec.len);
        strbuf_free(&sec);
        if (o->want_report || o->has_budget)
            fprintf(stderr,
                "cctxpack: est. tokens %zu (unified diff%s; full files would "
                "cost more)\n", est_of(&b), o->staged ? ", staged" : "");
    }
    {
        FILE *out = stdout;
        int ok = 1;
        if (o->output) {
            out = path_open(o->output, "wb");
            if (!out) {
                fprintf(stderr, "cctxpack: cannot write %s\n", o->output);
                ok = 0;
            }
        }
        if (ok) {
            /* out == NULL routes sink_write into the clipboard buffer */
            FILE *sink = o->clipboard ? NULL : out;
            if (sink_write(sink, &clip, b.data, b.len) != 0)
                out_rc = 1;
            else
                out_rc = finish_out(o, out, &clip);
        } else {
            out_rc = 1;
        }
    }
done:
    strbuf_free(&b);
    strbuf_free(&clip);
    free(text);
    free(top);
    return out_rc;
}

int pack_run(const pack_opts *o)
{
    if (o->patch)
        return pack_patch(o);
    int rc = 1;
    strlist paths, inc_paths;
    strbuf header, tree_all, tree_in, files_open, close;
    pack_item *items = NULL;
    size_t truncated = 0, used = 0, count = 0;

    strlist_init(&paths);
    strlist_init(&inc_paths);
    strbuf_init(&header);
    strbuf_init(&tree_all);
    strbuf_init(&tree_in);
    strbuf_init(&files_open);
    strbuf_init(&close);

    if (collect(o, &paths, &items) != 0)
        goto cleanup;

    /* The manifest reports post-fit totals, so the real header can only be
     * rendered after the fit.  The reserve therefore uses a PROBE of the
     * header, built with SIZE_MAX for every number: that renders the widest
     * possible digits, so the probe is never shorter than the real header and
     * a bundle cannot overshoot its budget by its own header. */
    strbuf probe;
    strbuf_init(&probe);
    {
        format_manifest worst;
        worst.commit = o->commit;
        worst.files = SIZE_MAX;
        worst.bytes = SIZE_MAX;
        worst.tokens = SIZE_MAX;
        worst.digest = 0;
        format_open(&probe, o->root_display, o->format, &worst);
    }
    size_t header_reserve = est_of(&probe);
    strbuf_free(&probe);

    format_files_open(&files_open, o->format);
    format_close(&close, o->format);
    if (o->want_tree)
        format_tree(&tree_all, paths.v, paths.n, o->format);

    if (o->has_budget) {
        size_t reserve = header_reserve + est_of(&tree_all)
            + est_of(&files_open) + est_of(&close);
        size_t remaining = o->budget > reserve ? o->budget - reserve : 0;
        for (size_t i = 0; i < paths.n; i++) {
            if (items[i].est <= remaining) {
                items[i].included = 1;
                remaining -= items[i].est;
                count++;
            }
        }
        if (count == 0) {
            fprintf(stderr,
                "cctxpack: nothing fits the budget (reserve %zu, budget %zu)\n",
                reserve, o->budget);
            report(o, items, paths.n, 0, 0, 0);
            rc = 3;
            goto cleanup;
        }
    } else {
        count = paths.n;
        for (size_t i = 0; i < paths.n; i++)
            items[i].included = 1;
    }

    for (size_t i = 0; i < paths.n; i++) {
        if (items[i].included)
            strlist_push(&inc_paths, items[i].path);
    }
    files_sort(&inc_paths);
    /* Called unconditionally: for JSON this closes the "tree" key that
     * format_open already emitted, which --no-tree used to leave dangling,
     * producing output no parser could read. */
    format_tree(&tree_in, o->want_tree ? inc_paths.v : NULL,
        o->want_tree ? inc_paths.n : 0, o->format);

    used = est_of(&tree_in) + est_of(&files_open) + est_of(&close);
    for (size_t i = 0; i < paths.n; i++) {
        if (items[i].included) {
            used += items[i].est;
            if (items[i].truncated)
                truncated++;
        }
    }

    /* Real header now that the fitted totals are known.  Its own estimate
     * lands in `used`, so the reported total is what the bundle actually
     * costs; the reserve above already covered it. */
    format_manifest man;
    manifest_of(o, items, paths.n, count, &man);
    format_open(&header, o->root_display, o->format, &man);
    used += est_of(&header);

    FILE *out = NULL;
    strbuf clip;
    strbuf_init(&clip);
    if (o->clipboard) {
        /* rendered into `clip`, then encoded for clip.exe below */
    } else if (o->output) {
        char *abs_out = path_abs(o->output);
        for (size_t i = 0; abs_out && i < paths.n; i++) {
            if (!items[i].included)
                continue;
            char full[4096];
            join_path(full, sizeof(full), o->root, items[i].path);
            char *abs_full = path_abs(full);
            if (abs_full && path_eq(abs_out, abs_full)) {
                fprintf(stderr, "cctxpack: -o %s overwrites an input file\n",
                    o->output);
                free(abs_full);
                free(abs_out);
                rc = 2;
                goto cleanup;
            }
            free(abs_full);
        }
        free(abs_out);
        out = path_open(o->output, "wb");
        if (!out) {
            fprintf(stderr, "cctxpack: cannot write %s\n", o->output);
            goto cleanup;
        }
    } else {
        out = stdout;
    }

    rc = 0;
    int wr = sink_write(out, &clip, header.data, header.len);
    wr |= sink_write(out, &clip, tree_in.data, tree_in.len);
    wr |= sink_write(out, &clip, files_open.data, files_open.len);
    int first_item = 1;
    for (size_t i = 0; i < paths.n && wr == 0; i++) {
        if (!items[i].included)
            continue;
        strbuf sec;
        strbuf_init(&sec);
        if (items[i].binary) {
            format_binary(&sec, o->format, first_item, items[i].path,
                (size_t)items[i].size);
        } else {
            char full[4096];
            join_path(full, sizeof(full), o->root, items[i].path);
            read_file rf;
            if (read_file_open(full, o->max_file_tokens, &rf) != 0) {
                fprintf(stderr, "cctxpack: cannot read %s\n", items[i].path);
                strbuf_free(&sec);
                goto cleanup;
            }
            format_section(&sec, o->format, first_item, items[i].path, rf.data,
                rf.len, items[i].tokens);
            read_file_free(&rf);
        }
        wr |= sink_write(out, &clip, sec.data, sec.len);
        strbuf_free(&sec);
        first_item = 0;
    }
    wr |= sink_write(out, &clip, close.data, close.len);
    if (wr != 0)
        rc = 1;

    /* `out` is NULL when the clipboard buffer is being filled */
    if (rc == 0 && finish_out(o, out, &clip) != 0)
        rc = 1;
    strbuf_free(&clip);
    if (rc == 0 && (o->want_report || o->has_budget))
        report(o, items, paths.n, count, truncated, used);

cleanup:
    strbuf_free(&header);
    strbuf_free(&tree_all);
    strbuf_free(&tree_in);
    strbuf_free(&files_open);
    strbuf_free(&close);
    strlist_free(&paths);
    strlist_free(&inc_paths);
    free(items);
    return rc;
}
