#ifndef CTX_READ_H
#define CTX_READ_H

#include <stddef.h>
#include <stdint.h>

#define READ_CAP_DEFAULT (4u * 1024 * 1024)
#define READ_SNIFF 8192

typedef struct {
    char *data;
    size_t len;
    uint64_t file_size;
    int binary;
    int truncated;
} read_file;

int read_file_open(const char *path, size_t max_tokens, read_file *out);
void read_file_free(read_file *r);

#endif
