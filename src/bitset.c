/*
 * bitset.c
 *
 *  Created on: Dec 2, 2025
 *      Author: yyan7
 */

#include "bitset.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* ----- internals ----- */
static inline size_t word_index(size_t v)  { return v / BITSET_WORD_BITS; }
static inline size_t bit_offset(size_t v)  { return v % BITSET_WORD_BITS; }

static inline unsigned long int full_ones(void) {
    /* build a word of all 1s portably */
    return ~(unsigned long int)0;
}

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) ++p;
    return p;
}

static int parse_nonneg_size_t(const char **pp, size_t *out) {
    const char *p = skip_ws(*pp);
    if (!isdigit((unsigned char)*p)) return -1;

    errno = 0;
    char *endptr = NULL;
    unsigned long val = strtoul(p, &endptr, 10);
    if (errno != 0 || endptr == p) return -1;
    *out = (size_t)val;
    *pp = endptr;
    return 0;
}

/* ----- public: lifecycle ----- */
int bitset_init(BitSet *b, size_t max_value) {
    b->max_value = max_value;

    /* inclusive range [0..max_value] needs (max_value+1) bits */
    size_t nbits  = max_value + 1;
    size_t nwords = (nbits + BITSET_WORD_BITS - 1) / BITSET_WORD_BITS;

    b->nwords = nwords;
    b->words  = (unsigned long int *)calloc(nwords, sizeof(unsigned long int));
    if (!b->words) {
        b->nwords = 0;
        b->max_value = 0;
        return -1;
    }
    return 0;
}

void bitset_free(BitSet *b) {
    free(b->words);
    b->words = NULL;
    b->nwords = 0;
    b->max_value = 0;
}

/* ----- public: basic ops ----- */
void bitset_clear_all(BitSet *b) {
    if (!b->words) return;
    memset(b->words, 0, b->nwords * sizeof(unsigned long int));
}

void bitset_set(BitSet *b, size_t value) {
    if (value > b->max_value) return;
    b->words[word_index(value)] |= (1UL << bit_offset(value));
}

void bitset_clear(BitSet *b, size_t value) {
    if (value > b->max_value) return;
    b->words[word_index(value)] &= ~(1UL << bit_offset(value));
}

void bitset_toggle(BitSet *b, size_t value) {
    if (value > b->max_value) return;
    b->words[word_index(value)] ^= (1UL << bit_offset(value));
}

int bitset_contains(const BitSet *b, size_t value) {
    if (value > b->max_value) return 0;
    return (int)((b->words[word_index(value)] >> bit_offset(value)) & 1UL);
}

/* ----- public: range ops (inclusive) ----- */
void bitset_set_range(BitSet *b, size_t start, size_t end) {
    if (start > end) { size_t t = start; start = end; end = t; }
    if (start > b->max_value) return;
    if (end   > b->max_value) end = b->max_value;

    size_t w0 = word_index(start), w1 = word_index(end);
    size_t o0 = bit_offset(start),  o1 = bit_offset(end);

    if (w0 == w1) {
        unsigned long int mask = ((o1 == BITSET_WORD_BITS - 1)
                                  ? full_ones()
                                  : ((1UL << (o1 + 1)) - 1UL));
        mask &= ~((1UL << o0) - 1UL);
        b->words[w0] |= mask;
        return;
    }

    /* first partial word */
    unsigned long int first_mask = ~( (1UL << o0) - 1UL );
    b->words[w0] |= first_mask;

    /* full middle words */
    for (size_t w = w0 + 1; w < w1; ++w) {
        b->words[w] = full_ones();
    }

    /* last partial word */
    unsigned long int last_mask = (o1 == BITSET_WORD_BITS - 1)
                                  ? full_ones()
                                  : ((1UL << (o1 + 1)) - 1UL);
    b->words[w1] |= last_mask;
}

void bitset_clear_range(BitSet *b, size_t start, size_t end) {
    if (start > end) { size_t t = start; start = end; end = t; }
    if (start > b->max_value) return;
    if (end   > b->max_value) end = b->max_value;

    size_t w0 = word_index(start), w1 = word_index(end);
    size_t o0 = bit_offset(start),  o1 = bit_offset(end);

    if (w0 == w1) {
        unsigned long int mask = ((o1 == BITSET_WORD_BITS - 1)
                                  ? full_ones()
                                  : ((1UL << (o1 + 1)) - 1UL));
        mask &= ~((1UL << o0) - 1UL);
        b->words[w0] &= ~mask;
        return;
    }

    unsigned long int first_mask = ~( (1UL << o0) - 1UL );
    b->words[w0] &= ~first_mask;

    for (size_t w = w0 + 1; w < w1; ++w) {
        b->words[w] = 0UL;
    }

    unsigned long int last_mask = (o1 == BITSET_WORD_BITS - 1)
                                  ? full_ones()
                                  : ((1UL << (o1 + 1)) - 1UL);
    b->words[w1] &= ~last_mask;
}

/* ----- public: parser ----- */
int bitset_parse_range_list(BitSet *b, const char *text) {
    const char *p = text;

    while (1) {
        size_t start, end;

        p = skip_ws(p);
        if (*p == '\0') return 0; /* done */

        if (parse_nonneg_size_t(&p, &start) != 0) return -1;

        p = skip_ws(p);
        if (*p == '-') {
            ++p;
            if (parse_nonneg_size_t(&p, &end) != 0) return -1;
        } else {
            end = start;
        }

        if (start > end) { size_t t = start; start = end; end = t; }
        if (end > b->max_value) return -2;

        bitset_set_range(b, start, end);

        p = skip_ws(p);
        if (*p == ',') { ++p; continue; }
        if (*p == '\0') return 0;
        return -1; /* unexpected char */
    }
}

/* ----- optional debug ----- */
void bitset_print(const BitSet *b) {
    printf("{ ");
    for (size_t i = 0; i <= b->max_value; ++i) {
        if (bitset_contains(b, i)) printf("%zu ", i);
    }
    printf("}\n");
}

