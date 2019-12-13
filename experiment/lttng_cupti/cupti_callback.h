//
// Created by Yonghong Yan on 12/12/19.
//

#ifndef PINSIGHT_CUPTI_CALLBACK_H
#define PINSIGHT_CUPTI_CALLBACK_H


#ifdef __cplusplus
extern "C" {
#endif
void LTTNG_CUPTI_Init(int rank);
void LTTNG_CUPTI_Fini(int rank);
#ifdef __cplusplus
}
#endif


#endif //PINSIGHT_CUPTI_CALLBACK_H
