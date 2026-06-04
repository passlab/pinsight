###############################################################################
# Makefile — HIP vecadd with ROCTracer tracing experiment
#
# Three build phases:
#   make vecadd          — Phase 1: pure HIP, no tracing
#   make vecadd_roctracer — Phase 2: ROCTracer callbacks + printf output
#   make vecadd_lttng    — Phase 3: ROCTracer callbacks + LTTng UST traces
#
# Run targets (Phase 2 and 3 only):
#   make run             — run Phase 1 (pure HIP)
#   make run_roctracer   — run Phase 2 (prints ROCTracer events to stdout)
#   make run_lttng       — run Phase 3 (collect LTTng traces, dump with babeltrace2)
###############################################################################

ROCM_PATH  ?= /opt/rocm-7.2.1
LTTNG_HOME ?= $(HOME)/local

HIPCC   := $(ROCM_PATH)/bin/hipcc
HIPFLAGS := -g -O2

ROCM_INC  := -I$(ROCM_PATH)/include
ROCM_LIB  := -L$(ROCM_PATH)/lib -lroctracer64 -lamdhip64

LTTNG_INC := -I$(LTTNG_HOME)/include -I.
LTTNG_LIB := -L$(LTTNG_HOME)/lib -Wl,-rpath,$(LTTNG_HOME)/lib -llttng-ust -ldl

TRACE_DIR := vecadd_lttng_traces

.PHONY: all run run_roctracer run_lttng clean allclean

all: vecadd vecadd_roctracer vecadd_lttng

# -----------------------------------------------------------------------
# Phase 1: pure HIP — roctracer_callback.cpp compiled as empty stubs
# -----------------------------------------------------------------------
vecadd: vecadd.hip roctracer_callback.cpp
	$(HIPCC) $(HIPFLAGS) $(ROCM_INC) -o $@ $^ $(ROCM_LIB)

# -----------------------------------------------------------------------
# Phase 2: ROCTracer callback API + printf
#   -DWITH_ROCTRACER activates the real roctracer code in roctracer_callback.cpp
# -----------------------------------------------------------------------
vecadd_roctracer: vecadd.hip roctracer_callback.cpp
	$(HIPCC) $(HIPFLAGS) $(ROCM_INC) -DWITH_ROCTRACER -o $@ $^ $(ROCM_LIB)

# -----------------------------------------------------------------------
# Phase 3: ROCTracer callback API + LTTng UST tracepoints
#   -DWITH_ROCTRACER enables roctracer, -DWITH_LTTNG switches printf → LTTng
# -----------------------------------------------------------------------
vecadd_lttng: vecadd.hip roctracer_callback.cpp lttng_roctracer_tracepoint.h
	$(HIPCC) $(HIPFLAGS) $(ROCM_INC) $(LTTNG_INC) \
	    -DWITH_ROCTRACER -DWITH_LTTNG \
	    -o $@ vecadd.hip roctracer_callback.cpp \
	    $(ROCM_LIB) $(LTTNG_LIB)

# -----------------------------------------------------------------------
# Run targets
# -----------------------------------------------------------------------
run: vecadd
	./vecadd

run_roctracer: vecadd_roctracer
	./vecadd_roctracer

run_lttng: vecadd_lttng
	lttng-sessiond --daemonize 2>/dev/null; sleep 0.3
	lttng create hip-trace --output=$(TRACE_DIR)
	lttng enable-event --userspace 'lttng_roctracer:*'
	lttng start
	./vecadd_lttng
	lttng stop
	lttng destroy
	@echo ""
	@echo "=== Trace events collected ==="
	babeltrace2 $(TRACE_DIR) | wc -l
	@echo ""
	@echo "=== Trace dump ==="
	babeltrace2 $(TRACE_DIR)

# -----------------------------------------------------------------------
clean:
	rm -f vecadd vecadd_roctracer vecadd_lttng *.o

allclean: clean
	rm -rf $(TRACE_DIR)
