/*
 * backtrace.h
 *
 *  Created on: Apr 3, 2023
 *      Author: yyan7
 */

#ifndef __SRC_BACKTRACE_H__
#define __SRC_BACKTRACE_H__

//For adding backtrace into trace record. They are defined in OMPT/PMPI/CUPTI, but properly guarded so only one place needs to define them
extern __thread void * backtrace_ip[];
extern __thread char ** backtrace_symbol;
extern __thread int backtrace_depth;

#define LTTNG_UST_TP_FIELDS_BACKTRACE \
        lttng_ust_field_sequence_hex(unsigned long int, backtrace, backtrace_ip, unsigned int, backtrace_depth)

//backtrace.c
extern void retrieve_backtrace();

#endif /* __SRC_BACKTRACE_H__ */
