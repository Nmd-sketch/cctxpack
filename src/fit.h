#ifndef CTX_FIT_H
#define CTX_FIT_H

#include <stddef.h>

/* Greedy first-fit: take items in order while they fit in `remaining`,
 * writing 1/0 into `included` and returning how many were taken.
 *
 * Deliberately simple and order-dependent rather than optimal (a knapsack
 * solve would reorder files); a bundle that silently drops a more relevant
 * file in favour of a cheaper earlier one is worse than one that is merely
 * not maximally full, and the caller controls relevance via ordering.
 */
size_t pack_fit(const size_t *ests, size_t n, size_t remaining,
    unsigned char *included);

#endif