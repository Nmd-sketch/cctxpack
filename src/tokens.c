#include "tokens.h"

/* Script-aware token estimator (design doc 4.6).
 *
 * The previous model charged every code point the same ASCII rate,
 * ceil((10*C + 35*L)/38).  Measured against tiktoken that is fine for ASCII
 * source but catastrophic elsewhere: CJK prose was -41% at the median and -85%
 * at the worst, because a Han character costs roughly one token, not 0.26.
 *
 * So the model now charges a per-script rate:
 *
 *   est = ceil( ( sum over classes of count_c * rate_c) / 1000 )
 *
 * `rate_c` is the median observed tokens-per-code-point for that class times
 * 1.25, rounded up, measured against cl100k_base and o200k_base over the
 * corpus recorded in design 4.6.  The 1.25 bias is deliberate: over-estimating
 * wastes a little budget, under-estimating overflows the context window.
 *
 * Everything is integer arithmetic over UTF-8 bytes with no locale use, so the
 * result is bit-identical on every platform (design principle P4).
 */

/* per-class rates, thousandths of a token per code point */
enum {
    CLS_ASCII = 0,   /* ASCII, excluding newline */
    CLS_NEWLINE,     /* '\n' */
    CLS_LATIN,       /* accented Latin */
    CLS_CYRILLIC,
    CLS_CJK,         /* Han + kana + Hangul */
    CLS_EMOJI,
    CLS_MARKS,       /* combining-mark scripts: Devanagari, Thai, ... */
    CLS_OTHER,       /* Greek, Arabic, Hebrew, symbols, anything unlisted */
    CLS_COUNT
};

static const unsigned short RATE[CLS_COUNT] = {
    /* CLS_ASCII    */ 264,
    /* CLS_NEWLINE  */ 1185,
    /* CLS_LATIN    */ 1389,
    /* CLS_CYRILLIC */ 377,
    /* CLS_CJK      */ 880,
    /* CLS_EMOJI    */ 2495,
    /* CLS_MARKS    */ 982,
    /* CLS_OTHER    */ 884
};

/* Decode one code point.  Invalid bytes decode to their own value so malformed
 * input still advances and can never loop. */
static unsigned cp_at(const unsigned char *s, size_t len, size_t i, size_t *adv)
{
    unsigned c = s[i];
    if (c < 0x80) {
        *adv = 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && i + 1 < len) {
        *adv = 2;
        return ((c & 0x1Fu) << 6) | (s[i + 1] & 0x3Fu);
    }
    if ((c & 0xF0) == 0xE0 && i + 2 < len) {
        *adv = 3;
        return ((c & 0x0Fu) << 12) | ((unsigned)(s[i + 1] & 0x3Fu) << 6)
            | (s[i + 2] & 0x3Fu);
    }
    if ((c & 0xF8) == 0xF0 && i + 3 < len) {
        *adv = 4;
        return ((c & 0x07u) << 18) | ((unsigned)(s[i + 1] & 0x3Fu) << 12)
            | ((unsigned)(s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3Fu);
    }
    *adv = 1;
    return c;
}

static int in(unsigned cp, unsigned lo, unsigned hi)
{
    return cp >= lo && cp <= hi;
}

static int classify(unsigned cp)
{
    if (cp == '\n')
        return CLS_NEWLINE;
    if (cp < 0x80)
        return CLS_ASCII;

    /* accented Latin (Latin-1 Supplement, Latin Extended A/B) */
    if (in(cp, 0x00C0, 0x024F) || in(cp, 0x1E00, 0x1EFF) || in(cp, 0x2C60, 0x2C7F)
        || in(cp, 0xA720, 0xA7FF))
        return CLS_LATIN;

    if (in(cp, 0x0400, 0x052F) || in(cp, 0x2DE0, 0x2DFF) || in(cp, 0xA640, 0xA69F))
        return CLS_CYRILLIC;

    /* combining-mark scripts */
    if (in(cp, 0x0900, 0x097F) || in(cp, 0x0980, 0x09FF) || in(cp, 0x0A80, 0x0AFF)
        || in(cp, 0x0B00, 0x0B7F) || in(cp, 0x0B80, 0x0BFF)
        || in(cp, 0x0C00, 0x0C7F) || in(cp, 0x0C80, 0x0CFF)
        || in(cp, 0x0D00, 0x0D7F) || in(cp, 0x0D80, 0x0DFF)
        || in(cp, 0x0E00, 0x0E7F) || in(cp, 0x0E80, 0x0EFF))
        return CLS_MARKS;

    /* Han, kana, Hangul */
    if (in(cp, 0x1100, 0x11FF) || in(cp, 0x3040, 0x30FF) || in(cp, 0x3130, 0x318F)
        || in(cp, 0x31F0, 0x31FF) || in(cp, 0x3400, 0x4DBF) || in(cp, 0x4E00, 0x9FFF)
        || in(cp, 0xA960, 0xA97F) || in(cp, 0xAC00, 0xD7AF) || in(cp, 0xF900, 0xFAFF)
        || in(cp, 0xFF66, 0xFF9F) || in(cp, 0x20000, 0x2A6DF))
        return CLS_CJK;

    /* emoji, pictographs, dingbats, variation selectors, regional indicators */
    if (in(cp, 0x203C, 0x203C) || in(cp, 0x2049, 0x2049) || in(cp, 0x2122, 0x2122)
        || in(cp, 0x2139, 0x2139) || in(cp, 0x2194, 0x21AA) || in(cp, 0x231A, 0x231B)
        || in(cp, 0x2328, 0x2328) || in(cp, 0x23CF, 0x23CF) || in(cp, 0x23E9, 0x23FA)
        || in(cp, 0x24C2, 0x24C2) || in(cp, 0x25AA, 0x25FE) || in(cp, 0x2600, 0x27BF)
        || in(cp, 0x2934, 0x2935) || in(cp, 0x2B00, 0x2BFF) || in(cp, 0x3030, 0x3030)
        || in(cp, 0x303D, 0x303D) || in(cp, 0x3297, 0x3297) || in(cp, 0x3299, 0x3299)
        || in(cp, 0xFE0F, 0xFE0F) || in(cp, 0x1F000, 0x1FAFF))
        return CLS_EMOJI;

    return CLS_OTHER;
}

size_t tokens_estimate(const char *s, size_t len)
{
    const unsigned char *b = (const unsigned char *)s;
    unsigned long long num = 0;
    size_t i = 0;

    while (i < len) {
        size_t adv;
        unsigned cp = cp_at(b, len, i, &adv);
        num += RATE[classify(cp)];
        i += adv;
    }
    return (size_t)((num + 999u) / 1000u);
}

size_t tokens_estimate_ascii(size_t chars, size_t lines)
{
    /* ceil((chars * RATE_ASCII + lines * RATE_NEWLINE) / 1000) */
    unsigned long long num = 264ull * (unsigned long long)chars
        + 1185ull * (unsigned long long)lines;
    return (size_t)((num + 999u) / 1000u);
}