//
// Created by Yonghong Yan on 12/12/19.
//

#include <stdio.h>
#include <cuda.h>
#include <cupti.h>
#include "cupti_callback.h"

#ifdef PINSIGHT_MPI
int mpirank = 0 ;
#endif
#ifdef PINSIGHT_OPENMP
__thread int global_thread_num = 0;
__thread int omp_thread_num = 0;
#endif

#define TRACEPOINT_CREATE_PROBES
#define TRACEPOINT_DEFINE
#include "lttng_cupti_tracepoint.h"

void CUPTIAPI CUPTI_callback_lttng(void *userdata, CUpti_CallbackDomain domain,
                             CUpti_CallbackId cbid, const CUpti_CallbackData *cbInfo) {
    CUptiResult cuptiErr;
    const void * codeptr = NULL; // __builtin_return_address(1);

    // Data is collected only for the following API
    if ((cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020) ||
        (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000) ||
        (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaDeviceSynchronize_v3020) ||
        (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020))  {

        if (cbInfo->callbackSite == CUPTI_API_ENTER) {
            // for a kernel launch report the kernel name, otherwise use the API
            // function name.
            if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020 ||
                cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000) {
                const char * kernelName = cbInfo->symbolName;
    		printf("inside callback for kernelLaunch_begin: %s\n", kernelName);
                tracepoint(lttng_pinsight_cuda, cudaKernelLaunch_begin, codeptr, kernelName);
            } else if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020) {
                // Store parameters passed to cudaMemcpy
                const char * funName = cbInfo->functionName;
                cudaMemcpy_v3020_params *funcParams = (cudaMemcpy_v3020_params *)(cbInfo->functionParams);
                void * dst = funcParams->dst;
                const void * src = funcParams->src;
                unsigned int count = funcParams->count;
                int kind = funcParams->kind;
    		printf("inside callback for cudaMemcpy_begin: %s\n", funName);
                tracepoint(lttng_pinsight_cuda, cudaMemcpy_begin, codeptr, funName, dst, src, count, kind);
            }
        }

        if (cbInfo->callbackSite == CUPTI_API_EXIT) {
            if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020 ||
                cbid == CUPTI_RUNTIME_TRACE_CBID_cudaLaunchKernel_v7000) {
                const char * kernelName = cbInfo->symbolName;
    		printf("inside callback for kernelLaunch_end: %s\n", kernelName);
                tracepoint(lttng_pinsight_cuda, cudaKernelLaunch_end, codeptr, kernelName);
            } else if (cbid == CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020) {
                // Store parameters passed to cudaMemcpy
                const char * funName = cbInfo->functionName;
                int return_val = *((int*)cbInfo->functionReturnValue);
    		printf("inside callback for cudaMemcpy_end: %s\n", funName);
                tracepoint(lttng_pinsight_cuda, cudaMemcpy_end, codeptr, funName, return_val);
            }
        }
    }
}

CUpti_SubscriberHandle subscriber;

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

#if 0
#include "common.h"

#ifdef HAVE_DLFCN_H
# define __USE_GNU
# include <dlfcn.h>
# undef  __USE_GNU
#endif
#ifdef HAVE_STDIO_H
# include <stdio.h>
#endif
#ifdef HAVE_STDLIB_H
# include <stdlib.h>
#endif
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#include "wrapper.h"

#include "cuda_common.h"

static void CUPTIAPI Extrae_CUPTI_callback (void *udata, CUpti_CallbackDomain domain,
                                            CUpti_CallbackId cbid, const CUpti_CallbackData *cbinfo)
{
    if (!mpitrace_on || !Extrae_get_trace_CUDA())
        return;

    UNREFERENCED_PARAMETER(udata);

    /* We process only CUDA runtime calls */
    if (domain == CUPTI_CB_DOMAIN_RUNTIME_API)
    {

        /* Check which event we have been subscribed. If we find a match through the switch,
           we will call the hooks within the cuda_common.c providing the parameters from
           the callback info parameter cbinfo->functionParams. The parameters are specific
           to the routine that has been invoked. */
        switch (cbid)
        {
            case CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020:
            {
                cudaLaunch_v3020_params *p = (cudaLaunch_v3020_params*) cbinfo->functionParams;
                if (cbinfo->callbackSite == CUPTI_API_ENTER)
#if CUPTI_API_VERSION >= 3
                    Extrae_cudaLaunch_Enter (p->func);
#else
                    Extrae_cudaLaunch_Enter (p->entry);
#endif
                else if (cbinfo->callbackSite == CUPTI_API_EXIT)
                    Extrae_cudaLaunch_Exit ();
            }
                break;

            case CUPTI_RUNTIME_TRACE_CBID_cudaConfigureCall_v3020:
            {
                cudaConfigureCall_v3020_params *p = (cudaConfigureCall_v3020_params*) cbinfo->functionParams;
                if (cbinfo->callbackSite == CUPTI_API_ENTER)
                    Extrae_cudaConfigureCall_Enter (p->gridDim, p->blockDim, p->sharedMem, p->stream);
                else if (cbinfo->callbackSite == CUPTI_API_EXIT)
                    Extrae_cudaConfigureCall_Exit ();
            }
                break;

            case CUPTI_RUNTIME_TRACE_CBID_cudaThreadSynchronize_v3020:
            {
                if (cbinfo->callbackSite == CUPTI_API_ENTER)
                    Extrae_cudaThreadSynchronize_Enter ();
                else if (cbinfo->callbackSite == CUPTI_API_EXIT)
                    Extrae_cudaThreadSynchronize_Exit ();
            }
                break;

            case CUPTI_RUNTIME_TRACE_CBID_cudaStreamCreate_v3020:
            {
                cudaStreamCreate_v3020_params *p = (cudaStreamCreate_v3020_params*)cbinfo->functionParams;
                if (cbinfo->callbackSite == CUPTI_API_ENTER)
                    Extrae_cudaStreamCreate_Enter (p->pStream);
                else if (cbinfo->callbackSite == CUPTI_API_EXIT)
                    Extrae_cudaStreamCreate_Exit ();
            }
                break;

            case CUPTI_RUNTIME_TRACE_CBID_cudaDeviceSynchronize_v3020:
            case CUPTI_RUNTIME_TRACE_CBID_cudaStreamSynchronize_v3020:
            {
                cudaStreamSynchronize_v3020_params *p = (cudaStreamSynchronize_v3020_params *)cbinfo->functionParams;
                if (cbinfo->callbackSite == CUPTI_API_ENTER)
                    Extrae_cudaStreamSynchronize_Enter (p->stream);
                else if (cbinfo->callbackSite == CUPTI_API_EXIT)
                    Extrae_cudaStreamSynchronize_Exit ();
            }
                break;

            case CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020:
            {
                cudaMemcpy_v3020_params *p = (cudaMemcpy_v3020_params *)cbinfo->functionParams;
                if (cbinfo->callbackSite == CUPTI_API_ENTER)
                    Extrae_cudaMemcpy_Enter (p->dst, p->src, p->count, p->kind);
                else if (cbinfo->callbackSite == CUPTI_API_EXIT)
                    Extrae_cudaMemcpy_Exit ();
            }
                break;

            case CUPTI_RUNTIME_TRACE_CBID_cudaMemcpyAsync_v3020:
            {
                cudaMemcpyAsync_v3020_params *p = (cudaMemcpyAsync_v3020_params*) cbinfo->functionParams;
                if (cbinfo->callbackSite == CUPTI_API_ENTER)
                    Extrae_cudaMemcpyAsync_Enter (p->dst, p->src, p->count, p->kind, p->stream);
                else if (cbinfo->callbackSite == CUPTI_API_EXIT)
                    Extrae_cudaMemcpyAsync_Exit ();
            }
                break;

            case CUPTI_RUNTIME_TRACE_CBID_cudaDeviceReset_v3020:
            {
                if (cbinfo->callbackSite == CUPTI_API_EXIT)
                    Extrae_cudaDeviceReset_Exit();
                else
                    Extrae_cudaDeviceReset_Enter();
            }
                break;

            case CUPTI_RUNTIME_TRACE_CBID_cudaThreadExit_v3020:
            {
                if (cbinfo->callbackSite == CUPTI_API_EXIT)
                    Extrae_cudaThreadExit_Exit();
                else
                    Extrae_cudaThreadExit_Enter();
            }
                break;
        }
    }
}

void Extrae_CUDA_init (int rank)
{
    CUpti_SubscriberHandle subscriber;

    UNREFERENCED_PARAMETER(rank);

    /* Create a subscriber. All the routines will be handled at Extrae_CUPTI_callback */
    cuptiSubscribe (&subscriber, (CUpti_CallbackFunc) Extrae_CUPTI_callback, NULL);

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
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                         CUPTI_RUNTIME_TRACE_CBID_cudaLaunch_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                         CUPTI_RUNTIME_TRACE_CBID_cudaConfigureCall_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                         CUPTI_RUNTIME_TRACE_CBID_cudaThreadSynchronize_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                         CUPTI_RUNTIME_TRACE_CBID_cudaStreamCreate_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                         CUPTI_RUNTIME_TRACE_CBID_cudaStreamSynchronize_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                         CUPTI_RUNTIME_TRACE_CBID_cudaDeviceSynchronize_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                         CUPTI_RUNTIME_TRACE_CBID_cudaMemcpy_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                         CUPTI_RUNTIME_TRACE_CBID_cudaMemcpyAsync_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                         CUPTI_RUNTIME_TRACE_CBID_cudaDeviceReset_v3020);
    cuptiEnableCallback (1, subscriber, CUPTI_CB_DOMAIN_RUNTIME_API,
                         CUPTI_RUNTIME_TRACE_CBID_cudaThreadExit_v3020);
}

#endif
