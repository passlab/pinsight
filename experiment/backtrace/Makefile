
backtrace:
	gcc backtrace.c -g -o backtrace
	gcc backtrace_dlinfo.c -ldl -g -o backtrace_dlinfo
	gcc backtrace_libunwind.c -g -o backtrace_libunwind -lunwind

clean:
	rm backtrace backtrace_dlinfo backtrace_libunwind
