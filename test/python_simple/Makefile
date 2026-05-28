# PInsight Python Tracing Test
#
# Usage:
#   make trace PINSIGHT_BUILD_DIR=<path_to_build_dir>
#
# Requires: LTTng userspace session daemon (lttng-sessiond -d)

PINSIGHT_BUILD_DIR ?= ../../build

.PHONY: trace clean

trace:
	@echo "=== PInsight Python Tracing Test ==="
	@echo "Creating LTTng session..."
	lttng create python-simple-trace
	lttng enable-event --userspace "pysysmon_pinsight_lttng_ust:*"
	lttng start
	LD_PRELOAD=$(PINSIGHT_BUILD_DIR)/libpinsight.so \
	  PYTHONPATH=$(PINSIGHT_BUILD_DIR) \
	  python3 -m pinsight python_simple.py
	lttng stop
	@echo ""
	@echo "=== Trace Output ==="
	lttng view | head -50
	lttng destroy

clean:
	lttng destroy python-simple-trace 2>/dev/null || true
