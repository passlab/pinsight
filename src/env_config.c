// RTune environment variable querying functions.
// Copyright (c) 2018, PASSLab Team. All rights reserved.

#include "env_config.h"

long env_get_long(const char* str, long default_value) {
    long out = default_value;
    // strtol segfaults if given a NULL ptr. Check before use!
    if (str != NULL) {
        out = strtol(str, NULL, 0);
    }
    // Error occurred in parsing, return default value.
    if (errno != 0) {
        out = default_value;
    }
    return out;
}

unsigned long env_get_ulong(const char* str, unsigned long default_value) {
    unsigned long out = default_value;
    // strtoul segfaults if given a NULL ptr. Check before use!
    if (str != NULL) {
        out = strtoul(str, NULL, 0);
    }
    // Error occurred in parsing, return default value.
    if (errno != 0) {
        out = default_value;
    }
    return out;
}
