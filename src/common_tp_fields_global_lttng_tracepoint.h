//
// Created by Yonghong Yan on 3/3/20.
//

#ifndef PINSIGHT_COMMON_TP_FIELDS_GLOBAL_LTTNG_TRACEPOINT_H
#define PINSIGHT_COMMON_TP_FIELDS_GLOBAL_LTTNG_TRACEPOINT_H

extern unsigned int pid;
extern char hostname[];

#define COMMON_TP_FIELDS_GLOBAL \
            ctf_string(hostname, hostname) \
            ctf_integer(unsigned int, pid, pid)

#endif //PINSIGHT_COMMON_TP_FIELDS_GLOBAL_LTTNG_TRACEPOINT_H:w

