//
// Created by Yonghong Yan on 12/12/19.
//

#include <stdio.h>
#include <cuda.h>
#include <cupti.h>
#include "pinsight.h"
#include "CUPTI_domain.h"

int CUDA_domain_index;
domain_info_t *CUDA_domain_info;
trace_config_t *CUDA_trace_config;

#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "cupti_lttng_ust_tracepoint.h"

void CUPTIAPI CUPTI_callback_lttng(void *userdata, CUpti_CallbackDomain domain,
                             CUpti_CallbackId cbid, const CUpti_CallbackData *cbInfo) {
    CUptiResult cuptiErr;
    const CUcontext context = cbInfo->context;
    unsigned int cxtId;  cuptiGetContextId (context, &cxtId);
    //CUdevice device; cuCtxGetDevice(&device);
    unsigned int devId; //cudaGetDevice(&devId);
    cuptiGetDeviceId(context, &devId);
    unsigned int correlationId = cbInfo->correlationId;

    if (domain != CUPTI_CB_DOMAIN_RUNTIME_API) {
        return;
    }

    if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020 ||
        cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000) {
        const void *codeptr = __builtin_return_address(9);
        //9 is the calldepth from the user program that makes kernel call to this point, i.e. 9 function call from user program to driver and to this callback
        //This is found by comparing looking at each backtrace address and their offset and see which one can be addr2line-ed to the user program
        //The same way is used to find the calldepth for cudaMemcpy, which is 6
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        const char *kernelName = cbInfo->symbolName;
        cudaLaunchKernel_v7000_params *p = (cudaLaunchKernel_v7000_params*) cbInfo->functionParams;
        if (cbInfo->callbackSite == CUPTI_API_ENTER) {
            //printf("inside callback for kernelLaunch_begin: %s\n", kernelName);
            cudaStream_t stream = p->stream;
            unsigned int streamId; cuptiGetStreamIdEx(context, stream, 0, &streamId);
            struct contextStreamId_t ctxStreamId; ctxStreamId.contextId = cxtId; ctxStreamId.streamId = streamId;
            uint64_t timeStamp; cuptiGetTimestamp(&timeStamp);
            dim3 grid = p->gridDim;
            dim3 block = p->blockDim;
            unsigned int gridx = grid.x;
            unsigned int gridy = grid.y;
            unsigned int gridz = grid.z;
            unsigned int blockx = block.x;
            unsigned int blocky = block.y;
            unsigned int blockz = block.z;
            struct dimension_t dim; dim.gridx = gridx; dim.gridy = gridy; dim.gridz = gridz; dim.blockx = blockx; dim.blocky = blocky; dim.blockz = blockz;
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaKernelLaunch_begin, devId, correlationId, timeStamp, codeptr, kernelName, &ctxStreamId, &dim);
        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
            //printf("inside callback for kernelLaunch_end: %s\n", kernelName);
            uint64_t timeStamp; cuptiGetTimestamp(&timeStamp);
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaKernelLaunch_end, devId, correlationId, timeStamp, codeptr, kernelName);
        } else {
            //ignore
        }
        return;
    }

    if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020) {
        const void *codeptr = __builtin_return_address(6);
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        const char *funName = cbInfo->functionName;
        if (cbInfo->callbackSite == CUPTI_API_ENTER) {
            // Store parameters passed to cudaMemcpy
            cudaMemcpy_v3020_params *funcParams = (cudaMemcpy_v3020_params * )(cbInfo->functionParams);
            void *dst = funcParams->dst;
            const void *src = funcParams->src;
            unsigned int count = funcParams->count;
            int kind = funcParams->kind;
            //printf("inside callback for cudaMemcpy_begin: %s\n", funName);
            uint64_t timeStamp; cuptiGetTimestamp(&timeStamp);
            /** Here we did not separate the cudaMemcpy kind in order to reduce the overhead of doing that, and separating memcpy kind
             * is done in the trace analysis
             */
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpy_begin, devId, correlationId, timeStamp, codeptr, funName, dst, src, count, kind);
        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
            int return_val = *((int *) cbInfo->functionReturnValue);
            //printf("inside callback for cudaMemcpy_end: %s\n", funName);
            uint64_t timeStamp; cuptiGetTimestamp(&timeStamp);
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpy_end, devId, correlationId, timeStamp, codeptr, funName, return_val);
        } else {
            //ignore
        }
        return;
    }
    if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaMemcpyAsync_v3020) {
        const void *codeptr = __builtin_return_address(6);
#ifdef PINSIGHT_BACKTRACE
        retrieve_backtrace();
#endif
        const char *funName = cbInfo->functionName;
        cudaMemcpyAsync_v3020_params *p = (cudaMemcpyAsync_v3020_params *) cbInfo->functionParams;
        if (cbInfo->callbackSite == CUPTI_API_ENTER) {
            void *dst = p->dst;
            const void *src = p->src;
            unsigned int count = p->count;
            int kind = p->kind;
            cudaStream_t stream = p->stream;
            unsigned int streamId;
            //cuptiGetStreamId(context, stream, &streamId);
            cuptiGetStreamIdEx(context, stream, 0, &streamId);
            struct contextStreamId_t ctxStreamId; ctxStreamId.contextId = cxtId; ctxStreamId.streamId = streamId;
            //printf("cudaMemcpyAsync callback via CUPTI ContextID:StreamID: %d:%d, stream: %ld\n", cxtId, streamId, (long int)stream);
            uint64_t timeStamp; cuptiGetTimestamp(&timeStamp);
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpyAsync_begin, devId, correlationId, timeStamp, codeptr, funName, dst, src, count, kind, &ctxStreamId);
        } else if (cbInfo->callbackSite == CUPTI_API_EXIT) {
            int return_val = *((int *) cbInfo->functionReturnValue);
            uint64_t timeStamp; cuptiGetTimestamp(&timeStamp);
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpyAsync_end, devId, correlationId, timeStamp, codeptr, funName, return_val);
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

static CUpti_SubscriberHandle subscriber;

void LTTNG_CUPTI_Init (int rank) {
    /* Create a subscriber, no need userData for bookkeeping  */
    cuptiSubscribe (&subscriber, (CUpti_CallbackFunc) CUPTI_callback_lttng, NULL);

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
}

void LTTNG_CUPTI_Fini (int rank) {
    cuptiUnsubscribe(subscriber);
}
