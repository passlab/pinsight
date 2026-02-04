#ifndef TRACE_CONFIG_PARSE_H
#define TRACE_CONFIG_PARSE_H

#include "trace_config.h"

// Main entry point to read and parse the configuration file
void parse_trace_config_file(char* filename);

// Helper function to trim whitespace from a string
char* trim_whitespace(char* str);

// Serialization functions
void print_domain_trace_config(FILE *out);
void print_lexgion_trace_config(FILE *out);

#endif // TRACE_CONFIG_PARSE_H
