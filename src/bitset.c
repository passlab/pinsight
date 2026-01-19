#include "bitset.h"

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>

#define BITS_PER_WORD (sizeof(unsigned long int) * 8)
#define WORD_INDEX(bit) ((bit) / BITS_PER_WORD)
#define BIT_MASK(bit)   (1UL << ((bit) % BITS_PER_WORD))

/* Inline-mode test: nbits < 64 => store bits in pointer value itself */
static int bitset_is_inline(const BitSet *bs)
{
    return bs && (bs->nbits < BITS_PER_WORD);
}

/* Get word i, abstracting over inline vs heap */
static unsigned long int bitset_get_word(const BitSet *bs, size_t index)
{
    if (!bs) return 0;

    if (bitset_is_inline(bs)) {
        if (index > 0) return 0;
        return (unsigned long int)(uintptr_t)(bs->words);
    } else {
        if (!bs->words || index >= bs->nwords) return 0;
        return bs->words[index];
    }
}

/* Set word i, abstracting over inline vs heap */
static void bitset_set_word(BitSet *bs, size_t index, unsigned long int value)
{
    if (!bs) return;

    if (bitset_is_inline(bs)) {
        if (index > 0) return;
        bs->words = (unsigned long int *)(uintptr_t)value;
    } else {
        if (!bs->words || index >= bs->nwords) return;
        bs->words[index] = value;
    }
}

/* ---- Public API ---- */

void bitset_init(BitSet *bs, size_t nbits)
{
    if (!bs) return;

    bs->nbits  = nbits;

    if (nbits < BITS_PER_WORD) {
        /* Inline storage: store bits in pointer value itself */
        bs->nwords = 1;
        bs->words  = (unsigned long int *)(uintptr_t)0;  /* all bits clear */
    } else {
        /* Heap storage */
        bs->nwords = WORD_INDEX(nbits) + 1;
        bs->words  = (unsigned long int *)calloc(bs->nwords,
                                                 sizeof(unsigned long int));
        if (!bs->words) {
            bs->nwords = 0;
        }
    }
}

void bitset_free(BitSet *bs)
{
    if (!bs) return;

    if (!bitset_is_inline(bs) && bs->words) {
        free(bs->words);
    }

    bs->words  = NULL;
    bs->nbits  = 0;
    bs->nwords = 0;
}

void bitset_reset(BitSet *bs)
{
    if (!bs) return;

    if (bitset_is_inline(bs)) {
        bs->words = (unsigned long int *)(uintptr_t)0;
    } else {
        if (!bs->words) return;
        memset(bs->words, 0, bs->nwords * sizeof(unsigned long int));
    }
}

void bitset_set(BitSet *bs, size_t bit)
{
    if (!bs) return;
    if (bit > bs->nbits) return;

    size_t widx = WORD_INDEX(bit);
    unsigned long int word = bitset_get_word(bs, widx);
    word |= BIT_MASK(bit);
    bitset_set_word(bs, widx, word);
}

void bitset_clear(BitSet *bs, size_t bit)
{
    if (!bs) return;
    if (bit > bs->nbits) return;

    size_t widx = WORD_INDEX(bit);
    unsigned long int word = bitset_get_word(bs, widx);
    word &= ~BIT_MASK(bit);
    bitset_set_word(bs, widx, word);
}

int bitset_test(const BitSet *bs, size_t bit)
{
    if (!bs) return 0;
    if (bit > bs->nbits) return 0;

    size_t widx = WORD_INDEX(bit);
    unsigned long int word = bitset_get_word(bs, widx);
    return (word & BIT_MASK(bit)) != 0;
}

/* ------ Helpers ------ */

static size_t popcount_ulong(unsigned long int x)
{
    size_t cnt = 0;
    while (x) {
        x &= (x - 1); /* clear lowest set bit */
        cnt++;
    }
    return cnt;
}

size_t bitset_count(const BitSet *bs)
{
    if (!bs) return 0;

    size_t total = 0;
    for (size_t i = 0; i < bs->nwords; ++i) {
        unsigned long int w = bitset_get_word(bs, i);
        total += popcount_ulong(w);
    }
    return total;
}

int bitset_first_set(const BitSet *bs)
{
    if (!bs) return -1;

    for (size_t wi = 0; wi < bs->nwords; ++wi) {
        unsigned long int word = bitset_get_word(bs, wi);
        if (word == 0)
            continue;

        for (size_t b = 0; b < BITS_PER_WORD; ++b) {
            if (word & (1UL << b)) {
                size_t bit_index = wi * BITS_PER_WORD + b;
                if (bit_index <= bs->nbits)
                    return (int)bit_index;
                else
                    return -1;
            }
        }
    }
    return -1;
}

int bitset_next_set(const BitSet *bs, int prev)
{
    if (!bs) return -1;

    if (prev < 0)
        return bitset_first_set(bs);

    size_t start = (size_t)prev + 1;
    if (start > bs->nbits)
        return -1;

    size_t wi = WORD_INDEX(start);
    if (wi >= bs->nwords)
        return -1;

    size_t b = start % BITS_PER_WORD;

    /* first: same word from b onward */
    unsigned long int word = bitset_get_word(bs, wi);
    if (b > 0) {
        unsigned long int mask = ~0UL << b;
        word &= mask;
    }

    if (word != 0) {
        for (; b < BITS_PER_WORD; ++b) {
            if (word & (1UL << b)) {
                size_t bit_index = wi * BITS_PER_WORD + b;
                if (bit_index <= bs->nbits)
                    return (int)bit_index;
                else
                    return -1;
            }
        }
    }

    /* then: subsequent words */
    for (wi = wi + 1; wi < bs->nwords; ++wi) {
        word = bitset_get_word(bs, wi);
        if (word == 0)
            continue;
        for (size_t bb = 0; bb < BITS_PER_WORD; ++bb) {
            if (word & (1UL << bb)) {
                size_t bit_index = wi * BITS_PER_WORD + bb;
                if (bit_index <= bs->nbits)
                    return (int)bit_index;
                else
                    return -1;
            }
        }
    }

    return -1;
}

void bitset_or(BitSet *dst, const BitSet *a, const BitSet *b)
{
    if (!dst || !a || !b) return;
    if (dst->nbits != a->nbits || dst->nbits != b->nbits) return;

    for (size_t i = 0; i < dst->nwords; ++i) {
        unsigned long int wa = bitset_get_word(a, i);
        unsigned long int wb = bitset_get_word(b, i);
        bitset_set_word(dst, i, wa | wb);
    }
}

void bitset_and(BitSet *dst, const BitSet *a, const BitSet *b)
{
    if (!dst || !a || !b) return;
    if (dst->nbits != a->nbits || dst->nbits != b->nbits) return;

    for (size_t i = 0; i < dst->nwords; ++i) {
        unsigned long int wa = bitset_get_word(a, i);
        unsigned long int wb = bitset_get_word(b, i);
        bitset_set_word(dst, i, wa & wb);
    }
}

char *bitset_to_string(const BitSet *bs)
{
    if (!bs) return NULL;

    size_t len = bs->nbits + 1;  /* bits 0..nbits */
    char *s = (char *)malloc(len + 1);
    if (!s) return NULL;

    for (size_t i = 0; i <= bs->nbits; ++i) {
        size_t bit_index = bs->nbits - i;  /* print from highest down */
        s[i] = bitset_test(bs, bit_index) ? '1' : '0';
    }
    s[len] = '\0';
    return s;
}

/* -------- Range parser: "0,2-4,11,12,24-32" -> set bits -------- */

static int parse_uint(const char **p, unsigned long int *out)
{
    const char *s = *p;
    while (isspace((unsigned char)*s)) s++;

    if (!isdigit((unsigned char)*s))
        return -1;

    errno = 0;
    char *endptr = NULL;
    unsigned long int val = strtoul(s, &endptr, 10);
    if (errno != 0 || endptr == s)
        return -1;

    *out = val;
    *p = endptr;
    return 0;
}

int bitset_parse_ranges(BitSet *bs, const char *spec)
{
    if (!bs || !spec)
        return -1;

    bitset_reset(bs);

    const char *p = spec;

    while (*p) {
        /* skip spaces and commas */
        while (isspace((unsigned char)*p) || *p == ',')
            p++;
        if (*p == '\0')
            break;

        unsigned long int start = 0, end = 0;

        /* parse start */
        if (parse_uint(&p, &start) != 0)
            return -1;

        while (isspace((unsigned char)*p)) p++;

        if (*p == '-') {
            /* range: start-end */
            p++; /* skip '-' */
            while (isspace((unsigned char)*p)) p++;
            if (parse_uint(&p, &end) != 0)
                return -1;
            if (end < start)
                return -1;
        } else {
            /* single value */
            end = start;
        }

        if (end > bs->nbits)
            return -1;

        for (unsigned long int i = start; i <= end; ++i) {
            bitset_set(bs, (size_t)i);
        }

        while (isspace((unsigned char)*p)) p++;
        if (*p == ',') {
            p++;
        } else if (*p == '\0') {
            break;
        } else {
            return -1;
        }
    }

    return 0;
}

/* -------- Range pretty-printer: BitSet -> "0,2-4,11,12,24-32" -------- */

char *bitset_to_rangestring(const BitSet *bs)
{
    if (!bs) return NULL;

    /* Worst-case buffer: each bit alone "N," -> ~6 chars per bit */
    size_t max_bits = bs->nbits + 1;
    size_t bufsize  = max_bits * 6 + 1;
    char *buf = (char *)malloc(bufsize);
    if (!buf)
        return NULL;

    size_t len = 0;
    buf[0] = '\0';

    int idx = bitset_first_set(bs);
    while (idx != -1) {
        int start = idx;
        int end   = idx;

        /* extend contiguous range */
        int next = bitset_next_set(bs, end);
        while (next != -1 && next == end + 1) {
            end = next;
            next = bitset_next_set(bs, end);
        }

        if (len > 0 && len < bufsize - 1) {
            buf[len++] = ',';
            buf[len] = '\0';
        }

        if (start == end) {
            len += (size_t)snprintf(buf + len, bufsize - len, "%d", start);
        } else {
            len += (size_t)snprintf(buf + len, bufsize - len, "%d-%d",
                                    start, end);
        }

        idx = next;
    }

    if (len == 0) {
        buf[0] = '\0';
    }

    return buf;
}
