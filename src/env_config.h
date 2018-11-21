// PInsight environment variable definitions, and safe querying functions.
// Copyright (c) 2018, PASSLab Team. All rights reserved.

#ifndef ENV_CONFIG_H
#define ENV_CONFIG_H

#include <errno.h>
#include <stdlib.h>


// --------------------------------------------------------
// Environment config variables

#define PINSIGHT_DEBUG "PINSIGHT_DEBUG"
#define PINSIGHT_DEBUG_DEFAULT 0


// --------------------------------------------------------
// Safe environment variable query functions

long env_get_long(const char* str, long default_value);
unsigned long env_get_ulong(const char* str, unsigned long default_value);

#endif // ENV_CONFIG_H
