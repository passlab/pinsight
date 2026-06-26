ROCM_PATH  ?= /opt/rocm-7.2.1
LTTNG_HOME ?= $(HOME)/local

HIPCC    := $(ROCM_PATH)/bin/hipcc
HIPFLAGS := -g -O2 -I$(ROCM_PATH)/include

# PInsight root and HIP-enabled build directory
PINSIGHT_ROOT := $(abspath ../..)
PINSIGHT_BUILD := $(PINSIGHT_ROOT)/build_hip
PINSIGHT_LIB  := $(PINSIGHT_BUILD)/libpinsight.so

TRACE_DIR := vecadd_hip_traces

.PHONY: all build_pinsight build test verify clean allclean

all: build

# -----------------------------------------------------------------------
# Build PInsight with HIP support (run once; skipped if lib already exists)
# -----------------------------------------------------------------------
build_pinsight: $(PINSIGHT_LIB)

$(PINSIGHT_LIB):
	mkdir -p $(PINSIGHT_BUILD)
	cd $(PINSIGHT_BUILD) && \
	cmake \
	    -DCMAKE_C_COMPILER=gcc \
	    -DCMAKE_CXX_COMPILER=g++ \
	    -DCMAKE_C_FLAGS="-I$(LTTNG_HOME)/include" \
	    -DCMAKE_CXX_FLAGS="-I$(LTTNG_HOME)/include" \
	    -DCMAKE_SHARED_LINKER_FLAGS="-L$(LTTNG_HOME)/lib -Wl,-rpath,$(LTTNG_HOME)/lib" \
	    -DPINSIGHT_OPENMP=TRUE \
	    -DPINSIGHT_MPI=FALSE \
	    -DPINSIGHT_CUDA=FALSE \
	    -DPINSIGHT_HIP=TRUE \
	    -DROCM_PATH=$(ROCM_PATH) \
	    -DCMAKE_PREFIX_PATH=$(LTTNG_HOME) \
	    $(PINSIGHT_ROOT) && \
	make -j$$(nproc)

# -----------------------------------------------------------------------
# Build the test applications
# -----------------------------------------------------------------------
build: vecadd_pinsight looping_pinsight

vecadd_pinsight: vecadd_pinsight.hip
	$(HIPCC) $(HIPFLAGS) -o $@ $^ -L$(ROCM_PATH)/lib -lamdhip64

# long-running loop for runtime-mode / activity-pool cycling tests
looping_pinsight: looping_pinsight.hip
	$(HIPCC) $(HIPFLAGS) --offload-arch=gfx942 -o $@ $^ -L$(ROCM_PATH)/lib -lamdhip64

# cyclic TRACING<->MONITORING test (drives the activity-pool disable/re-enable path)
cyclic: looping_pinsight $(PINSIGHT_LIB)
	bash cyclic_mode_test.sh

# -----------------------------------------------------------------------
# Run with LTTng tracing via PInsight
# -----------------------------------------------------------------------
test: vecadd_pinsight $(PINSIGHT_LIB)
	lttng-sessiond --daemonize 2>/dev/null; sleep 0.3
	lttng create hip-pinsight-test --output=$(TRACE_DIR)
	lttng enable-event --userspace 'roctracer_pinsight_lttng_ust:*'
	lttng enable-event --userspace 'pinsight_enter_exit_lttng_ust:*'
	lttng start
	LD_LIBRARY_PATH=$(LTTNG_HOME)/lib:$(ROCM_PATH)/lib:$$LD_LIBRARY_PATH \
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
	rm -rf $(TRACE_DIR) cyclic_traces cyclic_traces.applog
