#include "cli.h"

#include "format.h"
#include "util.h"

#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef CTX_VERSION
#define CTX_VERSION "0.0.0-dev"
#endif

enum {
    OPT_BUDGET = 1000,
    OPT_INCLUDE,
    OPT_EXCLUDE,
    OPT_MAXFT,
    OPT_REPORT,
    OPT_FORMAT,
    OPT_TREE,
    OPT_NOTREE,
    OPT_SORT,
    OPT_QUERY,
    OPT_CLIPBOARD,
    OPT_STAGED,
    OPT_PATCH,
    OPT_REDACT,
    OPT_OUTLINE,
    OPT_CONFIG,
    OPT_FIND,
    OPT_HELP,
    OPT_VERSION
};

static const struct option longopts[] = {
    { "budget", required_argument, NULL, OPT_BUDGET },
    { "include", required_argument, NULL, OPT_INCLUDE },
    { "exclude", required_argument, NULL, OPT_EXCLUDE },
    { "max-file-tokens", required_argument, NULL, OPT_MAXFT },
    { "report", no_argument, NULL, OPT_REPORT },
    { "format", required_argument, NULL, OPT_FORMAT },
    { "tree", no_argument, NULL, OPT_TREE },
    { "no-tree", no_argument, NULL, OPT_NOTREE },
    { "sort", required_argument, NULL, OPT_SORT },
    { "query", required_argument, NULL, OPT_QUERY },
    { "clipboard", no_argument, NULL, OPT_CLIPBOARD },
    { "staged", no_argument, NULL, OPT_STAGED },
    { "patch", no_argument, NULL, OPT_PATCH },
    { "redact", no_argument, NULL, OPT_REDACT },
    { "outline", no_argument, NULL, OPT_OUTLINE },
    { "config", no_argument, NULL, OPT_CONFIG },
    { "find", required_argument, NULL, OPT_FIND },
    { "help", no_argument, NULL, OPT_HELP },
    { "version", no_argument, NULL, OPT_VERSION },
    { NULL, 0, NULL, 0 }
};

void cli_help(void)
{
    printf("cctxpack — pack a repo into an LLM-ready context bundle\n"
           "\n"
           "usage:\n"
           "  cctxpack [pack] [paths...]   pack (default command)\n"
           "  cctxpack count [paths...]    per-file bytes/lines/tokens table\n"
           "  cctxpack tree [path]         render the directory tree\n"
           "  cctxpack diff [--staged]     pack files changed since HEAD\n"
           "\n"
           "options:\n"
           "  --budget N            token budget (est.); auto-enables the report\n"
           "  --format F            md | xml | json | txt (default: md)\n"
           "  --include G           include only matching paths (glob, repeatable)\n"
           "  --exclude G           exclude matching paths (glob, repeatable)\n"
           "  --max-file-tokens N   truncate files above N est. tokens\n"
           "  --sort S              name | mtime | size (default: name)\n"
           "  --query TEXT          re-rank candidates by keyword overlap\n"
           "  --tree / --no-tree    include the directory tree (default: on, off for diff)\n"
           "  --report              print the budget report on stderr\n"
           "  -o FILE               write the bundle to FILE (default: stdout)\n"
           "  --clipboard           copy the bundle to the Windows clipboard\n"
           "  --staged              diff: staged changes only\n"
           "  --patch               diff: emit the unified diff instead of whole files\n"
           "  --redact              replace credential-shaped text with\n"
           "                        [REDACTED:kind] markers (opt-in; see NOTES)\n"
           "  --outline             send declarations instead of whole files;\n"
           "                        with --budget, whole files first and\n"
           "                        outlines for whatever does not fit\n"
           "  --config              print the resolved configuration and exit\n"
           "  --find PATTERN        fixed-string search; print path:line: text\n"
           "                        for each match (grep -F style), then exit\n"
           "\n"
           "  diff reports staged, unstaged and untracked changes by default;\n"
           "  deleted files are never included.\n"
           "\n"
           "  Files above 4 MiB are read only up to that limit, and the bundle\n"
           "  says so: count reports bytes on disk AND bytes actually packed,\n"
           "  and any truncation appears in the manifest.  --max-file-tokens\n"
           "  still applies on top of it.\n"
           "\n"
           "  --help                show this help\n"
           "  --version             show version\n");
}

static int oom(void)
{
    fprintf(stderr, "cctxpack: out of memory\n");
    return 1;
}

static int usage_error(const char *msg, const char *arg)
{
    fprintf(stderr, "cctxpack: %s%s%s\n", msg, arg ? ": " : "",
        arg ? arg : "");
    fprintf(stderr, "try 'cctxpack --help'\n");
    return 2;
}

static int parse_size(const char *s, size_t *out)
{
    if (!s[0])
        return 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
    }
    errno = 0;
    unsigned long long v = strtoull(s, NULL, 10);
    if (errno != 0 || v > (unsigned long long)SIZE_MAX)
        return 0;
    *out = (size_t)v;
    return 1;
}

static void strip_arg(char *p)
{
    path_normalize(p);
}

/* Normalize a glob and append it, without touching the (possibly read-only)
 * argv string.  Returns 0, or -1 on allocation failure. */
static int push_norm(strlist *l, const char *arg)
{
    char *g = str_dup(arg);
    if (!g)
        return -1;
    path_normalize(g);
    int rc = strlist_push(l, g);
    free(g);
    return rc;
}

static int is_abs_path(const char *p)
{
    return p[0] == '/' || (p[0] != '\0' && p[1] == ':');
}

static int path_is_dir(const char *path, int *exists)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        *exists = 0;
        return 0;
    }
    *exists = 1;
#ifdef _WIN32
    return (st.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode);
#endif
}

static char *dirname_dup(const char *path)
{
    const char *slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            slash = p;
    }
    if (!slash)
        return str_dup(".");
    size_t len = (size_t)(slash - path);
    if (len == 0)
        len = 1;
    char *d = malloc(len + 1);
    if (!d)
        return NULL;
    memcpy(d, path, len);
    d[len] = '\0';
    return d;
}

static char *basename_dup(const char *path)
{
    const char *slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            slash = p;
    }
    return str_dup(slash ? slash + 1 : path);
}

static char *display_of(const char *root)
{
    if (strcmp(root, ".") == 0) {
        char *cwd = path_cwd();
        char *b = cwd ? basename_dup(cwd) : NULL;
        free(cwd);
        return b ? b : str_dup(".");
    }
    char *b = basename_dup(root);
    if (b && b[0])
        return b;
    free(b);
    return str_dup(root);
}

static int setup_paths(pack_opts *o, int argc, char **argv, int first)
{
    int n = argc - first;
    strlist args;
    strlist_init(&args);
    for (int i = first; i < argc; i++) {
        char *a = str_dup(argv[i]);
        if (!a) {
            strlist_free(&args);
            return oom();
        }
        strip_arg(a);
        if (strlist_push(&args, a) != 0) {
            free(a);
            strlist_free(&args);
            return oom();
        }
        free(a);
    }
    for (size_t i = 0; i < args.n; i++) {
        int exists = 0;
        path_is_dir(args.v[i], &exists);
        if (!exists) {
            int code = usage_error("no such path", args.v[i]);
            strlist_free(&args);
            return code;
        }
    }
    int rc = 0;
    if (n == 0) {
        o->root = str_dup(".");
    } else if (n == 1) {
        int exists = 0;
        if (path_is_dir(args.v[0], &exists)) {
            o->root = str_dup(args.v[0]);
        } else {
            o->root = dirname_dup(args.v[0]);
            char *base = basename_dup(args.v[0]);
            if (base && strlist_push(&o->path_args, base) != 0)
                rc = oom();
            free(base);
        }
    } else {
        o->root = str_dup(".");
        for (size_t i = 0; i < args.n; i++) {
            if (is_abs_path(args.v[i])) {
                rc = usage_error("absolute paths not allowed with "
                                 "multiple paths",
                    args.v[i]);
                break;
            }
        }
        for (size_t i = 0; rc == 0 && i < args.n; i++) {
            if (strlist_push(&o->path_args, args.v[i]) != 0)
                rc = oom();
        }
    }
    strlist_free(&args);
    if (rc != 0)
        return rc;
    if (!o->root)
        return oom();
    o->root_display = display_of(o->root);
    if (!o->root_display)
        return oom();
    return 0;
}

cli_action cli_parse(int argc, char **argv, const char *cmd, pack_opts *o,
    int *exit_code)
{
    strlist_init(&o->path_args);
    strlist_init(&o->include);
    strlist_init(&o->exclude);
    o->root = NULL;
    o->root_display = NULL;
    o->budget = 0;
    o->has_budget = 0;
    o->max_file_tokens = 0;
    o->max_tokens_set = 0;
    o->want_report = 0;
    /* FR-010: the tree is on for pack/count/tree, off for diff. */
    o->want_tree = strcmp(cmd, "diff") != 0;
    o->tree_set = 0;
    o->format = FMT_MD;
    o->fmt_set = 0;
    o->sort = SORT_NAME;
    o->sort_set = 0;
    o->clipboard = 0;
    o->staged = 0;
    o->patch = 0;
    o->redact = 0;
    o->outline = 0;
    o->query = NULL;
    o->output = NULL;
    o->fake_ls = NULL;
    o->find = NULL;

    opterr = 0;
    optind = 1;
    int c;
    while ((c = getopt_long(argc, argv, "o:", longopts, NULL)) != -1) {
        switch (c) {
        case 'o':
            o->output = optarg;
            break;
        case OPT_BUDGET:
            if (!parse_size(optarg, &o->budget)) {
                *exit_code = usage_error("--budget expects a number", optarg);
                return CLI_EXIT;
            }
            o->has_budget = 1;
            break;
        case OPT_MAXFT:
            if (!parse_size(optarg, &o->max_file_tokens)) {
                *exit_code = usage_error("--max-file-tokens expects a number",
                    optarg);
                return CLI_EXIT;
            }
            o->max_tokens_set = 1;
            break;
        case OPT_INCLUDE:
            if (!optarg[0]) {
                *exit_code = usage_error("empty --include glob", NULL);
                return CLI_EXIT;
            }
            if (push_norm(&o->include, optarg) != 0) {
                *exit_code = oom();
                return CLI_EXIT;
            }
            break;
        case OPT_EXCLUDE:
            if (!optarg[0]) {
                *exit_code = usage_error("empty --exclude glob", NULL);
                return CLI_EXIT;
            }
            if (push_norm(&o->exclude, optarg) != 0) {
                *exit_code = oom();
                return CLI_EXIT;
            }
            break;
        case OPT_REPORT:
            o->want_report = 1;
            break;
        case OPT_FORMAT: {
            int f = format_parse(optarg);
            if (f < 0) {
                *exit_code = usage_error("--format expects md|xml|json|txt",
                    optarg);
                return CLI_EXIT;
            }
            o->format = f;
            o->fmt_set = 1;
            break;
        }
        case OPT_TREE:
            o->want_tree = 1;
            o->tree_set = 1;
            break;
        case OPT_NOTREE:
            o->want_tree = 0;
            o->tree_set = 1;
            break;
        case OPT_SORT:
            if (strcmp(optarg, "name") == 0)
                o->sort = SORT_NAME;
            else if (strcmp(optarg, "mtime") == 0)
                o->sort = SORT_MTIME;
            else if (strcmp(optarg, "size") == 0)
                o->sort = SORT_SIZE;
            else {
                *exit_code = usage_error("--sort expects name|mtime|size",
                    optarg);
                return CLI_EXIT;
            }
            o->sort_set = 1;
            break;
        case OPT_QUERY:
            if (!optarg[0]) {
                *exit_code = usage_error("empty --query text", NULL);
                return CLI_EXIT;
            }
            o->query = optarg;
            break;
        case OPT_CLIPBOARD:
#ifdef _WIN32
            o->clipboard = 1;
            break;
#else
            *exit_code = usage_error("--clipboard is only available on Windows",
                NULL);
            return CLI_EXIT;
#endif
        case OPT_STAGED:
            o->staged = 1;
            break;
        case OPT_PATCH:
            o->patch = 1;
            break;
        case OPT_REDACT:
            o->redact = 1;
            break;
        case OPT_OUTLINE:
            o->outline = 1;
            break;
        case OPT_CONFIG:
            o->show_config = 1;
            break;
        case OPT_FIND:
            if (!optarg[0]) {
                *exit_code = usage_error("empty --find pattern", NULL);
                return CLI_EXIT;
            }
            o->find = optarg;
            break;
        case OPT_HELP:
            cli_help();
            *exit_code = 0;
            return CLI_EXIT;
        case OPT_VERSION:
            printf("cctxpack %s\n", CTX_VERSION);
            *exit_code = 0;
            return CLI_EXIT;
        default:
            fprintf(stderr, "cctxpack: unknown option\n");
            fprintf(stderr, "try 'cctxpack --help'\n");
            *exit_code = 2;
            return CLI_EXIT;
        }
    }

    if (o->clipboard && o->output) {
        *exit_code = usage_error("--clipboard and -o cannot be combined", NULL);
        return CLI_EXIT;
    }
    if (o->staged && strcmp(cmd, "diff") != 0) {
        *exit_code = usage_error("--staged is only valid with diff", NULL);
        return CLI_EXIT;
    }
    if (o->patch && strcmp(cmd, "diff") != 0) {
        *exit_code = usage_error("--patch is only valid with diff", NULL);
        return CLI_EXIT;
    }
    if (o->redact && strcmp(cmd, "tree") == 0) {
        /* tree never reads file content, so accepting the flag would be
         * silently doing nothing */
        *exit_code = usage_error("--redact is not valid with tree", NULL);
        return CLI_EXIT;
    }
    if (strcmp(cmd, "diff") == 0) {
        if (optind < argc) {
            *exit_code = usage_error("diff takes no paths", argv[optind]);
            return CLI_EXIT;
        }
        o->root = str_dup(".");
        if (!o->root) {
            *exit_code = oom();
            return CLI_EXIT;
        }
        o->root_display = display_of(o->root);
        if (!o->root_display) {
            *exit_code = oom();
            return CLI_EXIT;
        }
        return CLI_RUN;
    }

    int code = setup_paths(o, argc, argv, optind);
    if (code != 0) {
        *exit_code = code;
        return CLI_EXIT;
    }
    return CLI_RUN;
}
