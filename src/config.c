#include "config.h"

#include "cJSON.h"
#include "files.h"
#include "format.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define path_eq(a, b) (_stricmp((a), (b)) == 0)
#else
#define path_eq(a, b) (strcmp((a), (b)) == 0)
#endif

#define CONFIG_NAME ".cctxpack.json"

void config_init(config *c)
{
    memset(c, 0, sizeof(*c));
    strlist_init(&c->include);
    strlist_init(&c->exclude);
}

void config_free(config *c)
{
    strlist_free(&c->include);
    strlist_free(&c->exclude);
}

/* Parent of an absolute normalized path, or NULL at the root. */
static char *up_dir(const char *dir)
{
    size_t len = strlen(dir);
    while (len > 1 && dir[len - 1] == '/')
        len--;
    if (len < 2)
        return NULL;
    size_t cut = len;
    while (cut > 0 && dir[cut - 1] != '/')
        cut--;
    if (cut <= 1)
        return NULL;
    size_t keep = cut - 1;
    if (keep == 2 && dir[1] == ':')
        keep = 3; /* keep the "C:/" drive root */
    char *up = malloc(keep + 1);
    if (!up)
        return NULL;
    memcpy(up, dir, keep);
    up[keep] = '\0';
    if (path_eq(up, dir)) {
        free(up);
        return NULL;
    }
    return up;
}

static int file_exists(const char *path)
{
    FILE *f = path_open(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

int config_find(char **path)
{
    char *cwd = path_cwd();
    if (!cwd)
        return -1;
    path_normalize(cwd);
    char *top = NULL;
    int have_top = files_git_top(&top) == 0;
    char *dir = cwd;
    int rc = 0;
    while (dir) {
        size_t len = strlen(dir);
        char *cand = malloc(len + sizeof(CONFIG_NAME) + 2);
        if (!cand) {
            rc = -1;
            break;
        }
        sprintf(cand, "%s/%s", dir, CONFIG_NAME);
        if (file_exists(cand)) {
            *path = cand;
            rc = 1;
            break;
        }
        free(cand);
        if (have_top && path_eq(dir, top))
            break;
        char *up = up_dir(dir);
        free(dir);
        dir = up;
    }
    free(dir);
    free(top);
    return rc;
}

static int bad(const char *path, const char *key, const char *want)
{
    fprintf(stderr, "cctxpack: %s: %s must be %s\n", path, key, want);
    return 2;
}

static int num_key(const char *path, const cJSON *root, const char *key,
    size_t *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!v)
        return 0;
    if (!cJSON_IsNumber(v) || v->valuedouble < 0)
        return bad(path, key, "a non-negative number");
    *out = (size_t)v->valuedouble;
    return 1;
}

static int list_key(const char *path, const cJSON *root, const char *key,
    strlist *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!v)
        return 0;
    if (!cJSON_IsArray(v))
        return bad(path, key, "an array of glob strings");
    const cJSON *it = NULL;
    cJSON_ArrayForEach(it, v) {
        if (!cJSON_IsString(it) || !it->valuestring || !it->valuestring[0])
            return bad(path, key, "an array of glob strings");
        char *g = str_dup(it->valuestring);
        if (!g)
            return -1;
        path_normalize(g);
        int rc = strlist_push(out, g);
        free(g);
        if (rc != 0)
            return -1;
    }
    return 1;
}

int config_read(const char *path, config *c)
{
    FILE *f = path_open(path, "rb");
    if (!f) {
        fprintf(stderr, "cctxpack: cannot read %s\n", path);
        return 2;
    }
    strbuf sb;
    strbuf_init(&sb);
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (strbuf_appendn(&sb, buf, n) != 0) {
            fclose(f);
            strbuf_free(&sb);
            return -1;
        }
    }
    fclose(f);
    const char *end = NULL;
    cJSON *root = sb.data
        ? cJSON_ParseWithOpts(sb.data, &end, 1)
        : NULL;
    if (!root) {
        size_t off = end ? (size_t)(end - sb.data) : 0;
        fprintf(stderr, "cctxpack: %s: invalid JSON at offset %zu\n", path,
            off);
        strbuf_free(&sb);
        return 2;
    }

    int rc = 0;
    size_t num = 0;
    int r = num_key(path, root, "budget", &num);
    if (r < 0)
        rc = -1;
    else if (r == 2)
        rc = 2;
    else if (r == 1) {
        c->budget = num;
        c->has_budget = 1;
    }

    if (rc == 0) {
        r = num_key(path, root, "max_file_tokens", &num);
        if (r < 0)
            rc = -1;
        else if (r == 2)
            rc = 2;
        else if (r == 1) {
            c->max_file_tokens = num;
            c->has_max = 1;
        }
    }

    if (rc == 0) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "format");
        if (v) {
            int f = cJSON_IsString(v) && v->valuestring
                ? format_parse(v->valuestring)
                : -1;
            if (f < 0)
                rc = bad(path, "format", "one of md|xml|json|txt");
            else {
                c->format = f;
                c->has_format = 1;
            }
        }
    }

    if (rc == 0) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "tree");
        if (v) {
            if (!cJSON_IsBool(v))
                rc = bad(path, "tree", "a boolean");
            else {
                c->tree = cJSON_IsTrue(v) ? 1 : 0;
                c->has_tree = 1;
            }
        }
    }

    if (rc == 0) {
        r = list_key(path, root, "include", &c->include);
        rc = r == 2 ? 2 : (r < 0 ? -1 : 0);
    }
    if (rc == 0) {
        r = list_key(path, root, "exclude", &c->exclude);
        rc = r == 2 ? 2 : (r < 0 ? -1 : 0);
    }

    cJSON_Delete(root);
    strbuf_free(&sb);
    return rc;
}

int config_merge(const config *c, pack_opts *o)
{
    if (c->has_budget && !o->has_budget) {
        o->budget = c->budget;
        o->has_budget = 1;
    }
    if (c->has_max && !o->max_tokens_set) {
        o->max_file_tokens = c->max_file_tokens;
        o->max_tokens_set = 1;
    }
    if (c->has_tree && !o->tree_set) {
        o->want_tree = c->tree;
        o->tree_set = 1;
    }
    if (c->has_format && !o->fmt_set) {
        o->format = c->format;
        o->fmt_set = 1;
    }
    if (o->include.n == 0) {
        for (size_t i = 0; i < c->include.n; i++) {
            if (strlist_push(&o->include, c->include.v[i]) != 0)
                return -1;
        }
    }
    if (o->exclude.n == 0) {
        for (size_t i = 0; i < c->exclude.n; i++) {
            if (strlist_push(&o->exclude, c->exclude.v[i]) != 0)
                return -1;
        }
    }
    return 0;
}

int config_apply(pack_opts *o)
{
    char *path = NULL;
    int rc = config_find(&path);
    if (rc <= 0)
        return rc;
    config c;
    config_init(&c);
    rc = config_read(path, &c);
    if (rc == 0)
        rc = config_merge(&c, o);
    config_free(&c);
    free(path);
    return rc;
}

static void print_globs(const char *key, const strlist *l)
{
    if (l->n == 0) {
        printf("%s=\n", key);
        return;
    }
    for (size_t i = 0; i < l->n; i++)
        printf("%s%s=%s\n", key, (i == 0) ? "" : "+", l->v[i]);
}

void config_print(const pack_opts *o)
{
    char *path = NULL;
    if (config_find(&path) > 0)
        printf("file=%s\n", path);
    else
        printf("file=\n");
    free(path);

    printf("budget=%zu\n", o->has_budget ? o->budget : (size_t)0);
    printf("max_file_tokens=%zu\n",
        o->max_tokens_set ? o->max_file_tokens : (size_t)0);
    printf("format=%s\n", format_name(o->format));
    printf("sort=%s\n", (o->sort == SORT_MTIME) ? "mtime"
        : (o->sort == SORT_SIZE) ? "size" : "name");
    printf("tree=%d\n", (o->tree_set ? o->want_tree : 1) ? 1 : 0);
    print_globs("include", &o->include);
    print_globs("exclude", &o->exclude);
    printf("redact=%d\n", o->redact ? 1 : 0);
    printf("outline=%d\n", o->outline ? 1 : 0);
}
