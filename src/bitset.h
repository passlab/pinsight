#ifndef CONFIG_BITSET_H
#define CONFIG_BITSET_H

#include <stddef.h>

/*
 * BitSet:
 *   - If nbits < 64, the single word of bits is stored in the pointer
 *     value 'words' itself (no heap allocation).
 *   - If nbits >= 64, 'words' points to a heap-allocated array of
 *     'nwords' unsigned long int elements.
 */
typedef struct {
    unsigned long int *words;  /* inline-word (encoded in value) or pointer to array */
    size_t nbits;              /* maximum bit index (0..nbits) */
    size_t nwords;             /* number of words in use */
} BitSet;

/* Basic lifecycle */
void bitset_init(BitSet *bs, size_t nbits);
void bitset_free(BitSet *bs);
void bitset_reset(BitSet *bs);  /* clear all bits */

/* Single-bit operations */
void bitset_set(BitSet *bs, size_t bit);
void bitset_clear(BitSet *bs, size_t bit);
int  bitset_test(const BitSet *bs, size_t bit);

/* Helper operations */
size_t bitset_count(const BitSet *bs);

/* Find first set bit index (lowest), or -1 if none */
int bitset_first_set(const BitSet *bs);

/* Find next set bit after 'prev', or -1 if none */
int bitset_next_set(const BitSet *bs, int prev);

/* Bitwise OR / AND: dst = a OR b, dst = a AND b
   All bitsets must have compatible layout (same nwords, nbits). */
void bitset_or(BitSet *dst, const BitSet *a, const BitSet *b);
void bitset_and(BitSet *dst, const BitSet *a, const BitSet *b);

/* Convert to a binary string "111000..." (bits from highest to lowest).
 * Caller must free() the returned string.
 */
char *bitset_to_string(const BitSet *bs);

/* Parse a compressed range string like "0,2-4,11,12,24-32" into bs.
 * bs must already be initialized with a given nbits; if the string
 * refers to a bit > nbits, the function returns -1 (error).
 * On success, previous bits are cleared and only parsed bits are set.
 *
 * Returns 0 on success, -1 on error.
 */
int bitset_parse_ranges(BitSet *bs, const char *spec);

/* Pretty-print a bitset into a compressed range string like
 * "0,2-4,11,12,24-32" based on which bits are set.
 * Contiguous runs of length >= 2 are printed as "start-end".
 * Single bits are printed as "N".
 *
 * Returns heap-allocated string; caller must free().
 * Returns an empty string "" if no bits are set.
 */
char *bitset_to_rangestring(const BitSet *bs);

#endif /* CONFIG_BITSET_H */
