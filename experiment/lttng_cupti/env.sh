CUDA_INSTALL=/usr/local/cuda
CUPTI_INSTALL=${CUDA_INSTALL}/extras/CUPTI
export LD_LIBRARY_PATH=${CUPTI_INSTALL}/lib64:${CUDA_INSTALL}/lib64:${LD_LIBRARY_PATH}
