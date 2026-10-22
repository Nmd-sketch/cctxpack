#include "find.h"

#include "files.h"
#include "glob.h"
#include "read.h"

#include <stdio.h>
#include <string.h>

static void join_path(char *buf, size_t cap, const char *root, const char *rel)
{
    snprintf(buf, cap, "%s/%s", root, rel);
}

static int discover(const pack_opts *o, strlist *paths)
{
    if (files_discover(o->root, o->fake_ls, paths, NULL) != 0)
        return -1;
    root_filter(paths, &o->path_args);
    glob_filter(paths, &o->include, &o->exclude);
    return 0;
}

/* First offset of `needle` in `hay`, or SIZE_MAX.  memmem is POSIX and absent
 * on MSVC, so a small naive scan stands in; patterns are short and this runs
 * once per line. */
static size_t find_sub(const char *hay, size_t hn, const char *needle,
    size_t nn)
{
    if (nn == 0 || nn > hn)
        return nn == 0 ? 0 : (size_t)-1;
    for (size_t i = 0; i + nn <= hn; i++)
        if (memcmp(hay + i, needle, nn) == 0)
            return i;
    return (size_t)-1;
}

/* Emit `path:line: ` then the line body, stopping at the newline.  A line with
 * no trailing newline (last line of a file) is still emitted whole. */
static void emit_match(FILE *out, const char *path, size_t line,
    const char *body, size_t len)
{
    fprintf(out, "%s:%zu: ", path, line);
    fwrite(body, 1, len, out);
    fputc('\n', out);
}

int find_run(const pack_opts *o)
{
    strlist paths;
    strlist_init(&paths);
    if (discover(o, &paths) != 0) {
        fprintf(stderr, "cctxpack: cannot enumerate files under %s\n",
            o->root ? o->root : ".");
        strlist_free(&paths);
        return 1;
    }
    files_sort(&paths);

    FILE *out = stdout;
    if (o->output) {
        out = fopen(o->output, "wb");
        if (!out) {
            fprintf(stderr, "cctxpack: cannot open %s\n", o->output);
            strlist_free(&paths);
            return 1;
        }
    }

    const size_t plen = strlen(o->find);
    size_t matches = 0, files_hit = 0;
    for (size_t i = 0; i < paths.n; i++) {
        char full[4096];
        join_path(full, sizeof(full), o->root, paths.v[i]);
        read_file rf;
        if (read_file_open(full, 0, &rf) != 0)
            continue;
        if (rf.binary) {
            read_file_free(&rf);
            continue;
        }
        /* rf.packed is the content byte count; it excludes any truncation
         * marker that rf.len would include, so a marker can never be matched
         * or printed as if it were file text. */
        const char *data = rf.data;
        size_t len = rf.packed;
        size_t line = 1, hit = 0, pos = 0;
        while (pos < len) {
            size_t eol = pos;
            while (eol < len && data[eol] != '\n')
                eol++;
            if (find_sub(data + pos, eol - pos, o->find, plen) != (size_t)-1) {
                emit_match(out, paths.v[i], line, data + pos, eol - pos);
                hit++;
            }
            pos = (eol < len) ? eol + 1 : len;
            line++;
        }
        if (hit) {
            matches += hit;
            files_hit++;
        }
        read_file_free(&rf);
    }

    if (out != stdout)
        fclose(out);
    strlist_free(&paths);

    if (o->want_report)
        fprintf(stderr, "cctxpack: %zu matches in %zu files\n", matches,
            files_hit);
    return matches ? 0 : 4;
}
