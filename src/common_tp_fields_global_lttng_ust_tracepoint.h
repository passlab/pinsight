//
// Created by Yonghong Yan on 3/3/20.
//

#ifndef _PINSIGHT_COMMON_TP_FIELDS_GLOBAL_LTTNG_UST_TRACEPOINT_H_
#define _PINSIGHT_COMMON_TP_FIELDS_GLOBAL_LTTNG_UST_TRACEPOINT_H_

extern unsigned int pid;
extern char hostname[];

#define COMMON_LTTNG_UST_TP_FIELDS_GLOBAL \
            lttng_ust_field_string(hostname, hostname) \
            lttng_ust_field_integer(unsigned int, pid, pid)

#endif //_PINSIGHT_COMMON_TP_FIELDS_GLOBAL_LTTNG_UST_TRACEPOINT_H_

