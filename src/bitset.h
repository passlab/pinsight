/*
 * bitset.h
 *
 *  Created on: Dec 2, 2025
 *      Author: yyan7
 */

#ifndef SRC_BITSET_H_
#define SRC_BITSET_H_

#include <stddef.h>
#include <limits.h>

/* number of bits in one unsigned long int word */
#define BITSET_WORD_BITS (sizeof(unsigned long int) * CHAR_BIT)

typedef struct {
    unsigned long int *words; /* array of words */
    size_t nwords;            /* words array length */
    size_t max_value;         /* largest representable value (inclusive) */
} BitSet;

/* Construction / destruction */
int  bitset_init(BitSet *b, size_t max_value);  /* returns 0 on success, -1 on alloc failure */
void bitset_free(BitSet *b);

/* Basic ops */
void bitset_clear_all(BitSet *b);
void bitset_set(BitSet *b, size_t value);
void bitset_clear(BitSet *b, size_t value);
void bitset_toggle(BitSet *b, size_t value);
int  bitset_contains(const BitSet *b, size_t value); /* 0/1 */

/* Efficient inclusive range set/clear */
void bitset_set_range(BitSet *b, size_t start, size_t end);
void bitset_clear_range(BitSet *b, size_t start, size_t end);

/* Parser: e.g. "0,2-4,11,12,24-32" */
int bitset_parse_range_list(BitSet *b, const char *text);
/* Returns: 0 ok, -1 syntax error, -2 out-of-range value */

/* (Optional) debug print */
void bitset_print(const BitSet *b);

#endif /* SRC_BITSET_H_ */
