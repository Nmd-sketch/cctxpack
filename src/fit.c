/* Budget fitting, extracted from pack.c.
 *
 * A pure function over an array of estimates: no I/O, no policy, nothing that
 * knows what a file is.  Kept separate so the arithmetic can be tested on its
 * own and so pack.c is left with rendering and assembly.
 */

#include "fit.h"

size_t pack_fit(const size_t *ests, size_t n, size_t remaining,
    unsigned char *included)
{
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        if (ests[i] <= remaining) {
            included[i] = 1;
            remaining -= ests[i];
            count++;
        } else {
            included[i] = 0;
        }
    }
    return count;
}