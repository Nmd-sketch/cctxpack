#ifndef CTX_GLOB_H
#define CTX_GLOB_H

#include "files.h"

int glob_match(const char *path, const char *pattern);
void glob_filter(strlist *paths, const strlist *include, const strlist *exclude);
void root_filter(strlist *paths, const strlist *roots);

#endif
