#include "tokens.h"

/* est = ceil((C + 3.5*L) / 3.8), with alpha=3.5 and beta=3.8 the calibrated
 * constants recorded in docs/design-document.md 4.6.  The whole expression is
 * evaluated in integer arithmetic as ceil((10*C + 35*L) / 38) so the result is
 * bit-identical on every platform (design principle P4).  Inputs come from
 * files bounded by the read cap, so the products cannot overflow 64 bits. */
#define TOKENS_NUM_A 10u
#define TOKENS_NUM_B 35u
#define TOKENS_DEN 38u

size_t tokens_estimate_parts(size_t chars, size_t lines)
{
    unsigned long long num = TOKENS_NUM_A * (unsigned long long)chars
        + TOKENS_NUM_B * (unsigned long long)lines;
    return (size_t)((num + (TOKENS_DEN - 1u)) / TOKENS_DEN);
}

size_t tokens_estimate(const char *s, size_t len)
{
    size_t chars = 0;
    size_t lines = 0;
    for (size_t i = 0; i < len; i++) {
        if (((unsigned char)s[i] & 0xC0) != 0x80)
            chars++;
        if (s[i] == '\n')
            lines++;
    }
    return tokens_estimate_parts(chars, lines);
}
