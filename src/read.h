#ifndef CTX_READ_H
#define CTX_READ_H

#include <stddef.h>
#include <stdint.h>

#define READ_CAP_DEFAULT (4u * 1024 * 1024)
#define READ_SNIFF 8192

/* Why a file's content did not arrive whole.  The two causes need different
 * remedies, so a single boolean is not enough: the read cap is a memory-safety
 * bound, while the token cap is a request the user made. */
enum {
    TRUNC_NONE = 0,
    TRUNC_READ_CAP,      /* file exceeds READ_CAP_DEFAULT */
    TRUNC_TOKEN_CAP      /* --max-file-tokens, or the byte cap already applied */
};

const char *trunc_reason_name(int reason);

typedef struct {
    char *data;
    size_t len;          /* bytes in `data`, INCLUDING any appended marker */
    size_t packed;       /* content bytes actually read; excludes the marker */
    uint64_t file_size;  /* size on disk, which may exceed `packed` */
    int binary;
    int truncated;
    int trunc_reason;
} read_file;

int read_file_open(const char *path, size_t max_tokens, read_file *out);
void read_file_free(read_file *r);

#endif
