#ifndef CTX_SHIM_ERR_H
#define CTX_SHIM_ERR_H

#include <stdarg.h>
#include <stdio.h>

static inline void warnx(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "cctxpack: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

#endif
