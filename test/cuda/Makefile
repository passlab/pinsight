# CUDA counterpart of test/rocm/Makefile.
#
# Defaults target a distro CUDA install (nvcc + CUPTI in the system prefix).
# Override CUDA_PATH / CUPTI_LIB_DIR for a /usr/local/cuda-style toolkit.
CUDA_PATH     ?= $(shell dirname $(shell dirname $(shell which nvcc)))
CUDA_ARCH     ?= 75
# CUDA 13 moved CUPTI out of extras/CUPTI/lib64 into the targets/ tree; probe
# both, or an otherwise-fine toolkit yields an empty CUPTI_LIB_DIR (which
# `dirname` turns into ".") and the app fails to find libcupti at run time.
CUPTI_LIB_DIR ?= $(shell dirname $(firstword $(wildcard \
                     /usr/lib/x86_64-linux-gnu/libcupti.so \
                     $(CUDA_PATH)/extras/CUPTI/lib64/libcupti.so \
                     $(CUDA_PATH)/targets/x86_64-linux/lib/libcupti.so)))
LTTNG_HOME    ?=

NVCC     := $(CUDA_PATH)/bin/nvcc
NVFLAGS  := -g -O2 -arch=sm_$(CUDA_ARCH)

# PInsight root and CUDA-enabled build directory
PINSIGHT_ROOT  := $(abspath ../..)
PINSIGHT_BUILD ?= $(PINSIGHT_ROOT)/build_cuda
PINSIGHT_LIB   := $(PINSIGHT_BUILD)/libpinsight.so

TRACE_DIR := vecadd_cuda_traces

# LTTng from a custom prefix only if LTTNG_HOME is set (system install by default)
ifneq ($(LTTNG_HOME),)
LTTNG_CMAKE_ARGS := -DCMAKE_C_FLAGS="-I$(LTTNG_HOME)/include" \
                    -DCMAKE_CXX_FLAGS="-I$(LTTNG_HOME)/include" \
                    -DCMAKE_SHARED_LINKER_FLAGS="-L$(LTTNG_HOME)/lib -Wl,-rpath,$(LTTNG_HOME)/lib" \
                    -DCMAKE_PREFIX_PATH=$(LTTNG_HOME)
LTTNG_RUN_PATH   := $(LTTNG_HOME)/lib:
endif

.PHONY: all build_pinsight build test verify cyclic activity clean allclean

all: build

# -----------------------------------------------------------------------
# Build PInsight with CUDA support (run once; skipped if lib already exists)
# -----------------------------------------------------------------------
build_pinsight: $(PINSIGHT_LIB)

$(PINSIGHT_LIB):
	mkdir -p $(PINSIGHT_BUILD)
	cd $(PINSIGHT_BUILD) && \
	cmake \
	    -DCMAKE_C_COMPILER=gcc \
	    -DCMAKE_CXX_COMPILER=g++ \
	    -DPINSIGHT_OPENMP=TRUE \
	    -DPINSIGHT_MPI=TRUE \
	    -DPINSIGHT_CUDA=TRUE \
	    -DPINSIGHT_HIP=FALSE \
	    $(LTTNG_CMAKE_ARGS) \
	    $(PINSIGHT_ROOT) && \
	make -j$$(nproc)

# -----------------------------------------------------------------------
# Build the test applications
# -----------------------------------------------------------------------
build: vecadd_pinsight looping_pinsight

vecadd_pinsight: vecadd_pinsight.cu
	$(NVCC) $(NVFLAGS) -o $@ $^

# long-running loop for runtime-mode / activity-collection cycling tests
looping_pinsight: looping_pinsight.cu
	$(NVCC) $(NVFLAGS) -o $@ $^

# multi-stream probe for the CUPTI kernel-serialization check (commit 2384cd4);
# run it under PInsight and feed the trace to
# analysis/gpu_kernel_concurrency.py
concurrency_probe: concurrency_probe.cu
	$(NVCC) $(NVFLAGS) -o $@ $^

# cyclic TRACING<->MONITORING test (drives the activity disable/re-enable path)
cyclic: looping_pinsight $(PINSIGHT_LIB)
	bash cyclic_mode_test.sh

# device_activity node-policy gate: off / on / leader_per_node
activity: looping_pinsight $(PINSIGHT_LIB)
	bash device_activity_test.sh

# -----------------------------------------------------------------------
# Run with LTTng tracing via PInsight
# -----------------------------------------------------------------------
test: vecadd_pinsight $(PINSIGHT_LIB)
	lttng-sessiond --daemonize 2>/dev/null; sleep 0.3
	lttng create cuda-pinsight-test --output=$(TRACE_DIR)
	lttng enable-event --userspace 'cupti_pinsight_lttng_ust:*'
	lttng enable-event --userspace 'pinsight_enter_exit_lttng_ust:*'
	lttng add-context --userspace --type=vpid
	lttng start
	LD_LIBRARY_PATH=$(LTTNG_RUN_PATH)$(CUPTI_LIB_DIR):$$LD_LIBRARY_PATH \
	PINSIGHT_TRACE_CONFIG_FILE=$(CURDIR)/CUDA_trace_config.install \
	LD_PRELOAD=$(PINSIGHT_LIB) \
	    ./vecadd_pinsight 5
	lttng stop
	lttng destroy
	@echo ""
	@echo "=== Event count ==="
	babeltrace2 $(TRACE_DIR) | wc -l
	@echo ""
	@echo "=== Trace dump ==="
	babeltrace2 $(TRACE_DIR)

# -----------------------------------------------------------------------
# Verify the trace produced by 'make test'
# -----------------------------------------------------------------------
verify:
	bash verify_trace.sh $(TRACE_DIR)

# -----------------------------------------------------------------------
clean:
	rm -f vecadd_pinsight looping_pinsight

allclean: clean
	rm -rf $(TRACE_DIR) cyclic_traces cyclic_traces.applog activity_traces
