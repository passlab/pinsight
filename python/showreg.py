# Will need to update these imports when babeltrace 2.x comes out.
import babeltrace
import sys
from collections import defaultdict


# Get the trace path from the first command line argument.
trace_path = sys.argv[1]

trace_collection = babeltrace.TraceCollection()
trace_collection.add_traces_recursive(trace_path, 'ctf')

num_runs = defaultdict(int)
seen = set()
seen_order = []
for event in trace_collection.events:
    if event.name == "lttng_pinsight:parallel_begin":
        codeptr = event["codeptr_ra"]
        if codeptr not in seen:
            seen_order.append(codeptr)
        seen.add(codeptr)
        num_runs[codeptr] += 1

# Print region IDs and run counts.
print("RegionID, Num Runs")
for codeptr in seen_order:
    print("0x{:2X} :: {}".format(codeptr, num_runs[codeptr]))
