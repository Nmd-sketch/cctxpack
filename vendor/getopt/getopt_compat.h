#ifndef CTX_GETOPT_COMPAT_H
#define CTX_GETOPT_COMPAT_H

#include <stdint.h>

#ifndef __RCSID
#define __RCSID(x) extern int __ctx_getopt_rcsid
#endif

#ifndef __UNCONST
#define __UNCONST(a) ((void *)(uintptr_t)(const void *)(a))
#endif

#ifndef _DIAGASSERT
#define _DIAGASSERT(e) ((void)0)
#endif

#endif
