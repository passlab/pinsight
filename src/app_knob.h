#ifndef APP_KNOB_H
#define APP_KNOB_H

#include <stdio.h>

/**
 * Application Knobs: a config-driven named-value store.
 *
 * Knobs are defined in the [Knob] section of the PInsight config file.
 * Applications query them by name at their point of use, e.g.:
 *
 *   #pragma omp parallel num_threads(pinsight_get_knob_int("compute_threads"))
 *
 * Values are updated transparently via SIGUSR1 config reload.
 */

#define MAX_NUM_KNOBS 64
#define MAX_KNOB_NAME_LEN 64
#define MAX_KNOB_STRING_LEN 256

typedef enum {
    KNOB_TYPE_INT,
    KNOB_TYPE_DOUBLE,
    KNOB_TYPE_STRING,
} knob_type_t;

typedef union {
    int i;
    double d;
    char s[MAX_KNOB_STRING_LEN];
} knob_value_t;

typedef struct app_knob {
    char name[MAX_KNOB_NAME_LEN];
    knob_type_t type;
    knob_value_t value;
} app_knob_t;

extern app_knob_t knob_table[MAX_NUM_KNOBS];
extern int num_knobs;

/* --- Query API (called by application) --- */

/**
 * Get the integer value of a knob by name.
 * Returns 0 if the knob is not found.
 */
int pinsight_get_knob_int(const char *name);

/**
 * Get the double value of a knob by name.
 * Returns 0.0 if the knob is not found.
 */
double pinsight_get_knob_double(const char *name);

/**
 * Get the string value of a knob by name.
 * Returns "" if the knob is not found.
 */
const char *pinsight_get_knob_string(const char *name);

/* --- Internal API (called by config parser) --- */

/**
 * Find a knob by name. Returns index into knob_table, or -1 if not found.
 */
int pinsight_find_knob(const char *name);

/**
 * Find a knob by name, or create a new one if it doesn't exist.
 * Returns index into knob_table, or -1 if table is full.
 */
int pinsight_find_or_create_knob(const char *name, knob_type_t type);

/**
 * Set a knob value by index.
 */
void pinsight_set_knob_int(int idx, int value);
void pinsight_set_knob_double(int idx, double value);
void pinsight_set_knob_string(int idx, const char *value);

/**
 * Print all knob names and values to the given file.
 */
void pinsight_print_knob_config(FILE *out);

#endif /* APP_KNOB_H */
