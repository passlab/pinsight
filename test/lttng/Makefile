TRACE_LOCATION=traces

default: 
	gcc -o pthread_yield_suspend pthread_yield_suspend.c -llttng-ust -ldl -I. -lpthread -lm

test: default
	# Create a userspace session.
	lttng create pthread_yield_suspend-tracing-session --output="${TRACE_LOCATION}"
	# Create and enable event rules.
	lttng enable-event --userspace yield_suspend:'*'
	# Start LTTng tracing.
	lttng start
	# Run instrumented code.
	./pthread_yield_suspend
	# Stop LTTng tracing.
	lttng destroy

clean:
	rm -rf pthread_yield_suspend ${TRACE_LOCATION}
