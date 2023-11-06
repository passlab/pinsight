//
// Created by Yonghong Yan on 12/12/19.
//

#include <stdio.h>
#include <cuda.h>
#include <cupti.h>
#include <pthread.h>
#include "pinsight.h"

#define LTTNG_UST_TRACEPOINT_CREATE_PROBES
#define LTTNG_UST_TRACEPOINT_DEFINE
#include "cupti_lttng_ust_tracepoint.h"

typedef struct {
    int cid;
    void *dst;
    const void *src;
    unsigned int count;
    int kind;
    int stream;
    void* next;
    const void* codeptr;
    const char* funName;
}callback_data;

typedef struct {
    int size;
    int capacity;
    callback_data** buckets;
}hash_table;

hash_table hash_t;
pthread_mutex_t  mutex; //mutex associated with hashmap


int hash(hash_table* ht, int key) {
    return key % ht->capacity;
}

void hash_table_init(hash_table* ht, int capacity) {
    ht->buckets = (callback_data**)malloc(capacity*sizeof(callback_data*));
    ht->capacity = capacity;
    ht->size = capacity;
    for (int i = 0; i < capacity; i++) {
        ht->buckets[i] = NULL;
    }
}

void callback_data_init(callback_data* cbd) {
    cbd->cid = (int)NULL;
    cbd->src = NULL;
    cbd->dst = NULL;
    cbd->kind = (int)NULL;
    cbd->count = (int)NULL;
    cbd->stream = (int)NULL;
    cbd->codeptr = NULL;
    cbd->funName = NULL;
    cbd->next = NULL;
}

int hash_table_insert(hash_table* ht, callback_data* cbd) {
    int index = hash(ht, cbd->cid);
    if(ht->buckets[index] != NULL) {
        //printf("OCCUPIED");
        callback_data* node = ht->buckets[index];
        while (node->next != NULL) {
            //printf("next cid: %d\n",node->cid);
            node = node->next;
        }
        node->next = cbd;
    } else {
        //printf("EMPTY");
        ht->buckets[index] = cbd;
        return 1;
    }
    return 0;
}

int hash_table_remove(hash_table* ht, int key) {
    int index = hash(ht, key);
    if (ht->buckets[index] != NULL) {
        ht -> buckets[index] = NULL;
        return 1;
    }
    return 0;
}

callback_data* get(hash_table* ht, int key) {
    int index = hash(ht, key);
    callback_data* node = ht->buckets[index];
    if (node != NULL) {
        if (node->cid == key) {
            return node;
        } else {
            while (node->next != NULL) {
                node = node->next;
                if (node->cid == key) {
                    return node;
                }
            }
            return NULL;
        }
    }
    return NULL;
}

void hash_table_clean(hash_table* ht) {
    for (int i = 0; i < ht->capacity; i++) {
        free(ht->buckets[i]);
    }
    free(ht->buckets);
    //free(ht);
}

void print_hash_table(hash_table* ht) {
    printf("---START---\n");
    for (int i = 0; i < ht->size; i++) {
        if (ht->buckets[i] == NULL) {
            printf("cid = ---\n");
        } else {
            callback_data* node;
            node = ht->buckets[i];
            int depth = 1;
            printf("depth %d cid = %d\n",depth,node->cid);
            while (node->next != NULL) {
                depth += 1;
                node = node->next;
                printf("depth %d cid = %d\n",depth,node->cid);
            }
        }
    }
    printf("---END---\n");
}

void print_callback_data(callback_data* cbd) {
    printf("callback data:\n cid: %d\n dst: %p\n src: %p\n count: %d\n kind: %d\n stream: %d\n codept: %p\n funame: %s\n", cbd->cid,cbd->dst,cbd->src,cbd->count,cbd->kind,cbd->stream,cbd->codeptr,cbd->funName);
}

int try_write_trace(callback_data* data) {

    if (data != NULL) {
        if (data->cid != NULL &&
            data->dst != NULL &&
            data->dst != NULL &&
            data->kind != NULL &&
            data->stream != NULL &&
            data -> funName != NULL
            ) {
            print_callback_data(data);
            lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpy_begin, data->codeptr, data->funName, data->dst, data->src, data->count, data->stream, data->cid, data->kind);

            //printf("TRACEPOINT WRITTEN\n");
            return 1;
        }
        return 0;
    } else {
        return 0;
    }
}


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
            printf("test print sync\n");
            //lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpy_begin, codeptr, funName, dst, src, count, 123, 321,kind);
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
            int cid = cbInfo->correlationId;
            //printf("raw cid: %d\n", cid);

            //check if copy is in the hashmap. we use a mutex to make sure the other callback isn't accessing data at the sametime
            pthread_mutex_lock(&mutex);
            callback_data* data;
            data = get(&hash_t,cid);
            //print_callback_data(data); //crashing right here
            if (data  == NULL) {
                printf("null\n");
                data = (callback_data*)malloc(sizeof(callback_data));
                callback_data_init(data);
                data->cid = cid;
                data->dst = p->dst;
                data->src = p->src;
                data->count = p->count;
                data->kind = p->kind;
                data->codeptr = codeptr;
                data->funName = funName;
                hash_table_insert(&hash_t, data);
                //try_write_trace(data);
                pthread_mutex_unlock(&mutex);
            } else {
                data->cid = cid;
                data->dst = p->dst;
                data->src = p->src;
                data->count = p->count;
                data->kind = p->kind;
                data->codeptr = codeptr;
                data->funName = funName;
                printf("not null\n");
                pthread_mutex_unlock(&mutex);
                //lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpy_begin, codeptr, funName, dst, src, count, 123, 321, kind);
                //lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpy_begin, data->codeptr, data->funName, data->dst, data->src, data->count, data->stream, data->cid, data->kind);

                try_write_trace(data);
            }


            //printf("post creation cid: %d\n", data->cid);
            //printf("test print async\n");



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
            int cid = memcpyRecord->correlationId;
            pthread_mutex_lock(&mutex);
            callback_data* data;
            data = get(&hash_t,cid);
            //print_callback_data(data); //crashing right here
            if (data == NULL) {
                data = (callback_data*)malloc(sizeof(callback_data));
                callback_data_init(data);
                data->stream = memcpyRecord->streamId;
                hash_table_insert(&hash_t, data);
                printf("null\n");
                pthread_mutex_unlock(&mutex);
            } else {
                data->stream = memcpyRecord->streamId;
                printf("not null");
                pthread_mutex_unlock(&mutex);
                //lttng_ust_tracepoint(cupti_pinsight_lttng_ust, cudaMemcpy_begin, data->codeptr, data->funName, data->dst, data->src, data->count, data->stream, data->cid, data->kind);
                try_write_trace(data);
            }
        }

        status = cuptiActivityGetNextRecord(buffer, validSize, &record);
    }

    free(buffer);
}


static CUpti_SubscriberHandle subscriber;

void LTTNG_CUPTI_Init (int rank) {
    /*initialize hash map*/
    hash_table_init(&hash_t, 100);

    /*initialize mutex*/
    pthread_mutex_init(&mutex, NULL);
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
    cuptiActivityFlushAll(0);
    // clean up hash table
    print_hash_table(&hash_t);
    hash_table_clean(&hash_t);
 
}
