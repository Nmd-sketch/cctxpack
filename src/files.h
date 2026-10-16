#ifndef CTX_FILES_H
#define CTX_FILES_H

#include <stddef.h>

typedef struct {
    char **v;
    size_t n;
    size_t cap;
} strlist;

void strlist_init(strlist *l);
void strlist_free(strlist *l);
int strlist_push(strlist *l, const char *s);

#define FILES_ERR -1
#define FILES_NOGIT 1

/* Sort modes (FR-019). */
enum {
    SORT_NAME = 0,
    SORT_MTIME,
    SORT_SIZE
};

int files_git(const char *root, const char *fake_ls, strlist *out);
int files_walk(const char *root, strlist *out);
int files_discover(const char *root, const char *fake_ls, strlist *out,
    int *used_git);
void files_sort(strlist *l);
/* Stat-based ordering key: 0 for SORT_NAME or when stat fails, else the byte
 * size (SORT_SIZE, ascending) or the negated mtime (SORT_MTIME, newest first). */
long long files_sort_key(const char *root, const char *path, int mode);
void files_sort_mode(strlist *l, const char *root, int mode);
/* Absolute path of the enclosing git work tree, owned by the caller.  Returns
 * 0, FILES_NOGIT (not a work tree / git unavailable) or FILES_ERR. */
int files_git_top(char **out);
/* Paths changed versus HEAD (FR-013): unstaged by default, staged when
 * `staged`.  Returns 0 with `top` set, FILES_NOGIT or FILES_ERR. */
int files_git_diff(int staged, strlist *out, char **top);

#endif
