#include "files.h"

#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

void strlist_init(strlist *l)
{
    l->v = NULL;
    l->n = 0;
    l->cap = 0;
}

void strlist_free(strlist *l)
{
    for (size_t i = 0; i < l->n; i++)
        free(l->v[i]);
    free(l->v);
    strlist_init(l);
}

int strlist_push(strlist *l, const char *s)
{
    if (l->n == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 32;
        char **v = realloc(l->v, cap * sizeof(char *));
        if (!v)
            return -1;
        l->v = v;
        l->cap = cap;
    }
    char *dup = str_dup(s);
    if (!dup)
        return -1;
    l->v[l->n++] = dup;
    return 0;
}

static int split_push(strlist *l, const char *data, size_t len, char delim)
{
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || data[i] == delim) {
            if (i > start) {
                char *item = malloc(i - start + 1);
                if (!item)
                    return -1;
                memcpy(item, data + start, i - start);
                item[i - start] = '\0';
                path_normalize(item);
                if (strlist_push(l, item) != 0) {
                    free(item);
                    return -1;
                }
                free(item);
            }
            start = i + 1;
        }
    }
    return 0;
}

static int popen_run(const char *cmd, char **out, size_t *out_len)
{
#ifdef _WIN32
    FILE *p = _popen(cmd, "rb");
#else
    FILE *p = popen(cmd, "r");
#endif
    if (!p)
        return -1;
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) {
#ifdef _WIN32
        _pclose(p);
#else
        pclose(p);
#endif
        return -1;
    }
    for (;;) {
        if (len + 1024 > cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) {
                free(buf);
#ifdef _WIN32
                _pclose(p);
#else
                pclose(p);
#endif
                return -1;
            }
            buf = nb;
        }
        size_t got = fread(buf + len, 1, 1024, p);
        len += got;
        if (got < 1024)
            break;
    }
#ifdef _WIN32
    int rc = _pclose(p);
#else
    int rc = pclose(p);
#endif
    buf[len] = '\0';
    *out = buf;
    *out_len = len;
    return rc;
}

static int chdir_restore(const char *dir, char **saved)
{
    *saved = path_cwd();
    if (!*saved)
        return -1;
#ifdef _WIN32
    if (_chdir(dir) != 0)
#else
    if (chdir(dir) != 0)
#endif
    {
        free(*saved);
        *saved = NULL;
        return -1;
    }
    return 0;
}

static void chdir_back(const char *saved)
{
#ifdef _WIN32
    _chdir(saved);
#else
    chdir(saved);
#endif
    free((char *)saved);
}

int files_git(const char *root, const char *fake_ls, strlist *out)
{
    char *saved = NULL;
    if (chdir_restore(root, &saved) != 0)
        return FILES_ERR;
    int rc_code;
    if (fake_ls) {
        rc_code = split_push(out, fake_ls, strlen(fake_ls), '\n') == 0 ? 0 : -1;
        chdir_back(saved);
        return rc_code;
    }
    char *buf = NULL;
    size_t len = 0;
    int rc = popen_run("git rev-parse --is-inside-work-tree", &buf, &len);
    if (rc != 0 || strncmp(buf, "true", 4) != 0) {
        free(buf);
        chdir_back(saved);
        return FILES_NOGIT;
    }
    free(buf);
    rc = popen_run("git ls-files -c -o --exclude-standard -z", &buf, &len);
    if (rc != 0) {
        free(buf);
        chdir_back(saved);
        return FILES_ERR;
    }
    rc_code = split_push(out, buf, len, '\0') == 0 ? 0 : FILES_ERR;
    free(buf);
    chdir_back(saved);
    return rc_code;
}

#ifdef _WIN32
static int utf16_to_utf8(const wchar_t *w, char **out)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (len <= 0)
        return -1;
    char *s = malloc((size_t)len);
    if (!s)
        return -1;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, len, NULL, NULL);
    *out = s;
    return 0;
}

static int walk_dir(const char *base, const char *rel, strlist *out)
{
    char pattern_str[4096];
    snprintf(pattern_str, sizeof(pattern_str), "%s/*", base);
    wchar_t pattern[4096];
    int wlen = MultiByteToWideChar(CP_UTF8, 0, pattern_str, -1, pattern,
        (int)(sizeof(pattern) / sizeof(pattern[0])));
    if (wlen <= 0)
        return -1;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    int rc = 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        char *name = NULL;
        if (utf16_to_utf8(fd.cFileName, &name) != 0) {
            rc = -1;
            break;
        }
        int is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        int is_link = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        char child_rel[4096];
        if (rel[0])
            snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, name);
        else
            snprintf(child_rel, sizeof(child_rel), "%s", name);
        if (is_dir) {
            if (!is_link && strcmp(name, ".git") != 0) {
                char child_base[4096];
                snprintf(child_base, sizeof(child_base), "%s/%s", base, name);
                if (walk_dir(child_base, child_rel, out) != 0) {
                    free(name);
                    rc = -1;
                    break;
                }
            }
        } else if (strlist_push(out, child_rel) != 0) {
            free(name);
            rc = -1;
            break;
        }
        free(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return rc;
}
#else
static int walk_dir(const char *base, const char *rel, strlist *out)
{
    DIR *d = opendir(base);
    if (!d)
        return -1;
    int rc = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        char child_rel[4096];
        if (rel[0])
            snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, e->d_name);
        else
            snprintf(child_rel, sizeof(child_rel), "%s", e->d_name);
        unsigned char type = e->d_type;
        if (type == DT_UNKNOWN) {
            char child_base[4096];
            snprintf(child_base, sizeof(child_base), "%s/%s", base, e->d_name);
            struct stat st;
            if (lstat(child_base, &st) != 0)
                continue;
            type = S_ISDIR(st.st_mode) ? DT_DIR : DT_REG;
        }
        if (type == DT_DIR) {
            char child_base[4096];
            snprintf(child_base, sizeof(child_base), "%s/%s", base, e->d_name);
            if (strcmp(e->d_name, ".git") == 0)
                continue;
            if (walk_dir(child_base, child_rel, out) != 0) {
                rc = -1;
                break;
            }
        } else if (type == DT_REG) {
            if (strlist_push(out, child_rel) != 0) {
                rc = -1;
                break;
            }
        }
    }
    closedir(d);
    return rc;
}
#endif

int files_walk(const char *root, strlist *out)
{
    return walk_dir(root, "", out);
}

int files_discover(const char *root, const char *fake_ls, strlist *out,
    int *used_git)
{
    int rc = files_git(root, fake_ls, out);
    if (rc == FILES_ERR)
        return rc;
    int git = rc != FILES_NOGIT;
    if (!git && files_walk(root, out) != 0)
        return FILES_ERR;
    if (used_git)
        *used_git = git;
    return 0;
}

static int cmp_path(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

void files_sort(strlist *l)
{
    qsort(l->v, l->n, sizeof(char *), cmp_path);
}
