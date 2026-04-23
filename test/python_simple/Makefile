# Makefile for simple Python tracing example

# Path to the PInsight build directory containing pinsight.py and _pinsight_python.so
PINSIGHT_BUILD_DIR ?= ../../build
RUN_CMD = PYTHONPATH=$(PINSIGHT_BUILD_DIR) python3 -m pinsight

# Standard execution without tracing
run:
	python3 python_simple.py

# Trace execution using LTTng and PInsight wrapper
trace:
	@echo "Creating and starting LTTng tracing session for Python..."
	lttng create python-simple-trace
	lttng enable-event --userspace "python_pinsight_lttng_ust:*"
	lttng start
	$(RUN_CMD) python_simple.py
	lttng stop
	@echo "Viewing traces (omitting internal frames for brevity):"
	lttng view | grep "python_simple.py" | head -n 25
	lttng destroy
	@echo "Trace saved and session destroyed."

clean:
	rm -rf __pycache__
