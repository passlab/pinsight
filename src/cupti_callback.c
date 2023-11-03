//
// Created by Yonghong Yan on 12/12/19.
//

#include <stdio.h>
#include <cuda.h>
#include <cupti.h>
#include "pinsight.h"

#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "cupti_lttng_ust_tracepoint.h"

struct callback_data {
    void *dst;
    const void *src;
    unsigned int count;
    int kind;
    int stream;
    int cid;
};

void CUPTIAPI CUPTI_callback_lttng(void *userdata, CUpti_CallbackDomain domain,
                             CUpti_CallbackId cbid, const CUpti_CallbackData *cbInfo) {
    CUptiResult cuptiErr;
    const void *codeptr = NULL; // __builtin_return_address(1);

    if (domain != CUPTI_CB_DOMAIN_RUNTIME_API) {
        return;
    }

    if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020 ||
        cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000) {
        const char *kernelName = cbInfo->symbolName;
        if (cbInfo->callbackSite == CUPTI_API_ENTER) {
            //printf("inside callback for kernelLaunch_begin: %s\n", kernelName);
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaKernelLaunch_begin, codeptr, kernelName);
        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
            //printf("inside callback for kernelLaunch_end: %s\n", kernelName);
        	lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaKernelLaunch_end, codeptr, kernelName);
        } else {
            //ignore
        }
        return;
    }

    if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020) {
        const char *funName = cbInfo->functionName;
        if (cbInfo->callbackSite == CUPTI_API_ENTER) {
            // Store parameters passed to cudaMemcpy
            cudaMemcpy_v3020_params *funcParams = (cudaMemcpy_v3020_params * )(cbInfo->functionParams);
            void *dst = funcParams->dst;
            const void *src = funcParams->src;
            unsigned int count = funcParams->count;
            int kind = funcParams->kind;
            //printf("inside callback for cudaMemcpy_begin: %s\n", funName);
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpy_begin, codeptr, funName, dst, src, count, 123, 321,kind);
        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
            int return_val = *((int *) cbInfo->functionReturnValue);
            //printf("inside callback for cudaMemcpy_end: %s\n", funName);
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpy_end, codeptr, funName, return_val);
        } else {
            //ignore
        }
        return;
    }
    if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaMemcpyAsync_v3020) {
        //cudaMemcpyAsync_v3020_params *p = (cudaMemcpyAsync_v3020_params *) cbInfo->functionParams;

        const char *funName = cbInfo->functionName;
        if (cbInfo->callbackSite == CUPTI_API_ENTER) {
// Store parameters passed to cudaMemcpy
            cudaMemcpyAsync_v3020_params *p = (cudaMemcpyAsync_v3020_params *) cbInfo->functionParams;
            void *dst = p->dst;
            const void *src = p->src;
            unsigned int count = p->count;
            int kind = p->kind;
            int stream = cbInfo->correlationId;
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpy_begin, codeptr, funName, dst, src, count, 123, 321, kind);

            //printf("inside callback for cudaMemcpy_begin: %s\n", funName);

        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
            int return_val = *((int *) cbInfo->functionReturnValue);
            //printf("inside callback for cudaMemcpy_end: %s\n", funName);
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpy_end, codeptr, funName, return_val);

        } else {

        }
        return;
    }

    /* //CUDA 10.2 does not have cudaConfigureCall_v3020_params, could be its bug
    if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaConfigureCall_v3020) {
        cudaConfigureCall_v3020_params *p = (cudaConfigureCall_v3020_params *) cbInfo->functionParams;
        if (cbInfo->callbackSite == CUPTI_API_ENTER) {
            //p->gridDim, p->blockDim, p->sharedMem, p->stream;
        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
        } else {

        }
        return;
    }
    */

    if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaThreadSynchronize_v3020) {
        if (cbInfo->callbackSite == CUPTI_API_ENTER) {

        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {

        } else {

        }
        return;
    }

    if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaStreamCreate_v3020) {
        cudaStreamCreate_v3020_params *p = (cudaStreamCreate_v3020_params *) cbInfo->functionParams;
        if (cbInfo->callbackSite == CUPTI_API_ENTER) {
            //p->pStream
        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {

        } else {

        }
        return;
    }

    if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaDeviceSynchronize_v3020 ||
        cbid == CUPTI_RUNTIME_TRACE_CBID_cudaStreamSynchronize_v3020) {
        cudaStreamSynchronize_v3020_params *p = (cudaStreamSynchronize_v3020_params *) cbInfo->functionParams;
        if (cbInfo->callbackSite == CUPTI_API_ENTER) {

        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {

        } else {

        }
        return;
    }

    if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaDeviceReset_v3020) {
        if (cbInfo->callbackSite == CUPTI_API_ENTER) {

        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {

        } else {

        }
        return;
    }

    if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaThreadExit_v3020) {
        if (cbInfo->callbackSite == CUPTI_API_ENTER) {

        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {

        } else {

        }
        return;
    }
    return;
}

//allocated buffer to sore records from activity API
void CUPTIAPI bufferRequested(uint8_t **buffer, size_t *size, size_t *maxNumRecords) {
    *size = 16 * 1024;
    *buffer = (uint8_t *)malloc(*size);
}

//
void CUPTIAPI bufferCompleted(CUcontext ctx, uint32_t streamId, uint8_t *buffer, size_t size, size_t validSize) {
    CUpti_Activity *record = NULL;
    CUptiResult status = cuptiActivityGetNextRecord(buffer, validSize, &record);

    while (status == CUPTI_SUCCESS) {
        if (record->kind == CUPTI_ACTIVITY_KIND_MEMCPY) {
            CUpti_ActivityMemcpy *memcpyRecord = (CUpti_ActivityMemcpy *)record;
            printf("Detected cudaMemcpyAsync of %llu bytes on stream %u. CUPTI reported stream ID: %u.\n", 
                   (unsigned long long)memcpyRecord->bytes, 
                   memcpyRecord->streamId, 
                   streamId);
            //void *dst = memcpyRecord->dst;
            //const void *src = memcpyRecord->src;
            //unsigned int count = memcpyRecord->count;
            //int kind = memcpyRecord->kind;
            //lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpy_begin, codeptr, funName, dst, src, count, 0, kind);
            

         }

        status = cuptiActivityGetNextRecord(buffer, validSize, &record);
    }

    free(buffer);
}


static CUpti_SubscriberHandle subscriber;

void LTTNG_CUPTI_Init (int rank) {
    /* Create a subscriber, no need userData for bookkeeping  */
    cuptiSubscribe (&subscriber, (CUpti_CallbackFunc) CUPTI_callback_lttng, NULL);

    //register CUPTI activity callback
    cuptiActivityRegisterCallbacks(bufferRequested, bufferCompleted);
    /* Activate callbacks for the following API calls:
      CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020
      CUPTI_RUNTIME_TRACE_CBID_cudaConfigureCall_v3020
      CUPTI_RUNTIME_TRACE_CBID_cudaThreadSynchronize_v3020
      CUPTI_RUNTIME_TRACE_CBID_cudaStreamCreate_v3020
      CUPTI_RUNTIME_TRACE_CBID_cudaStreamSynchronize_v3020
      CUPTI_RUNTIME_TRACE_CBID_cudaDeviceSynchronize_v3020
      CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020
      CUPTI_RUNTIME_TRACE_CBID_cudaMemcpyAsync_v3020
      CUPTI_RUNTIME_TRACE_CBID_cudaDeviceReset_v3020
      CUPTI_RUNTIME_TRACE_CBID_cudaThreadExit_v3020 */
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API, CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API, CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API, CUPTI_RUNTIME_TRACE_CBID_cudaConfigureCall_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API, CUPTI_RUNTIME_TRACE_CBID_cudaThreadSynchronize_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API, CUPTI_RUNTIME_TRACE_CBID_cudaStreamCreate_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API, CUPTI_RUNTIME_TRACE_CBID_cudaStreamSynchronize_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API, CUPTI_RUNTIME_TRACE_CBID_cudaDeviceSynchronize_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API, CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API, CUPTI_RUNTIME_TRACE_CBID_cudaMemcpyAsync_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API, CUPTI_RUNTIME_TRACE_CBID_cudaDeviceReset_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API, CUPTI_RUNTIME_TRACE_CBID_cudaThreadExit_v3020);
    //enable activity
    cuptiActivityEnable(CUPTI_ACTIVITY_KIND_MEMCPY);
}

void LTTNG_CUPTI_Fini (int rank) {
    cuptiUnsubscribe(subscriber);
    // Flush any remaining activity records
   // cuptiActivityFlushAll(0);
}
