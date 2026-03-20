#include "app_knob.h"
#include <string.h>

app_knob_t knob_table[MAX_NUM_KNOBS];
int num_knobs = 0;

/* --- Internal helpers --- */

int pinsight_find_knob(const char *name) {
    for (int i = 0; i < num_knobs; i++) {
        if (strcmp(knob_table[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int pinsight_find_or_create_knob(const char *name, knob_type_t type) {
    int idx = pinsight_find_knob(name);
    if (idx >= 0) {
        return idx;
    }
    if (num_knobs >= MAX_NUM_KNOBS) {
        fprintf(stderr, "PInsight Warning: knob table full (%d), cannot create "
                        "knob '%s'\n",
                MAX_NUM_KNOBS, name);
        return -1;
    }
    idx = num_knobs++;
    memset(&knob_table[idx], 0, sizeof(app_knob_t));
    strncpy(knob_table[idx].name, name, MAX_KNOB_NAME_LEN - 1);
    knob_table[idx].name[MAX_KNOB_NAME_LEN - 1] = '\0';
    knob_table[idx].type = type;
    return idx;
}

/* --- Setters (called by parser) --- */

void pinsight_set_knob_int(int idx, int value) {
    if (idx < 0 || idx >= num_knobs)
        return;
    knob_table[idx].type = KNOB_TYPE_INT;
    knob_table[idx].value.i = value;
}

void pinsight_set_knob_double(int idx, double value) {
    if (idx < 0 || idx >= num_knobs)
        return;
    knob_table[idx].type = KNOB_TYPE_DOUBLE;
    knob_table[idx].value.d = value;
}

void pinsight_set_knob_string(int idx, const char *value) {
    if (idx < 0 || idx >= num_knobs)
        return;
    knob_table[idx].type = KNOB_TYPE_STRING;
    strncpy(knob_table[idx].value.s, value, MAX_KNOB_STRING_LEN - 1);
    knob_table[idx].value.s[MAX_KNOB_STRING_LEN - 1] = '\0';
}

/* --- Query API (called by application) --- */

int pinsight_get_knob_int(const char *name) {
    int idx = pinsight_find_knob(name);
    if (idx < 0)
        return 0;
    if (knob_table[idx].type == KNOB_TYPE_DOUBLE)
        return (int)knob_table[idx].value.d;
    return knob_table[idx].value.i;
}

double pinsight_get_knob_double(const char *name) {
    int idx = pinsight_find_knob(name);
    if (idx < 0)
        return 0.0;
    if (knob_table[idx].type == KNOB_TYPE_INT)
        return (double)knob_table[idx].value.i;
    return knob_table[idx].value.d;
}

static const char *empty_string = "";

const char *pinsight_get_knob_string(const char *name) {
    int idx = pinsight_find_knob(name);
    if (idx < 0)
        return empty_string;
    return knob_table[idx].value.s;
}

/* --- Diagnostics --- */

void pinsight_print_knob_config(FILE *out) {
    if (!out || num_knobs == 0)
        return;
    fprintf(out, "[Knob]  # %d knob(s) defined\n", num_knobs);
    for (int i = 0; i < num_knobs; i++) {
        app_knob_t *k = &knob_table[i];
        switch (k->type) {
        case KNOB_TYPE_INT:
            fprintf(out, "    %s = %d\n", k->name, k->value.i);
            break;
        case KNOB_TYPE_DOUBLE:
            fprintf(out, "    %s = %f\n", k->name, k->value.d);
            break;
        case KNOB_TYPE_STRING:
            fprintf(out, "    %s = %s\n", k->name, k->value.s);
            break;
        }
    }
    fprintf(out, "\n");
}
