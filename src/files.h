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

int files_git(const char *root, const char *fake_ls, strlist *out);
int files_walk(const char *root, strlist *out);
int files_discover(const char *root, const char *fake_ls, strlist *out,
    int *used_git);
void files_sort(strlist *l);

#endif
