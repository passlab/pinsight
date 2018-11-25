# Per-thread whole-program CSV generator script.
# Copyright (c) 2018, PASSLab Team. All rights reserved.

# We will need to update these imports when babeltrace 2.x comes out.
import babeltrace
import sys
import csv
import copy
from pprint import pprint


#---------------------------------------------------------
# Global variables

work_event_names = [
    "lttng_pinsight:parallel_begin",
    "lttng_pinsight:implicit_task_begin",
    "lttng_pinsight:work_begin",
]


#---------------------------------------------------------
# Utility functions

# Cite: https://stackoverflow.com/a/14981125
def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)


# HACK: Apparently, if we use the raw `event` object, there's some kind of copying bug.
#   To get around it, we convert the object's members into a dictionary.
def event_as_dict(e):
    out = dict(list(e.items()))
    out["name"] = e.name
    out["timestamp"] = e.timestamp
    return out


#---------------------------------------------------------
# Application functions

# Compute durations between events in each stream.
def add_durations(events: list):
    # Use a list comprehension to compute timestamp diffs in one go.
    # Note: This will not compute the proper diff for the last event in the series.
    ts_diffs = [b["timestamp"] - a["timestamp"]
                for (a, b) in zip(events[:-1], events[1:])]
    for i in range(0, len(events) - 1):
        events[i]["duration"] = ts_diffs[i]

    # Patch up last event manually.
    # TODO: Figure out how to properly get the duration of the last event.
    last_event = events[-1]
    last_event["duration"] = 1
    return events


# The function that actually generates per-thread statistics.
def gen_stats(events: list):
    total_exec_time = events[-1]["timestamp"] - events[0]["timestamp"]
    total_overhead = 0
    total_energy = 0

    # Figure out total overhead time by grinding through events linearly.
    for event in events:
        if event["name"] not in work_event_names:
            total_overhead += event["duration"]

    # Figure out energy differences per-package and sum.
    package_names = ["pkg_energy" + str(i) for i in range(0,4)]
    start_energy = [events[0][x] for x in package_names]
    end_energy = [events[-1][x] for x in package_names]
    for i in range(0,4):
        total_energy += end_energy[i] - start_energy[i]

    return [total_exec_time, total_overhead, total_energy]


if __name__ == "__main__":
    # Check for right number of arguments.
    if len(sys.argv) < 3:
        usage = "Usage:\n  python3 per_thread.py path/to/trace MASTER_THREAD_ID"
        eprint("Error: Too few arguments.\n"+usage)
        exit(1)
 
    # Get the trace path from the first command line argument.
    trace_path = sys.argv[1]
    master_thread_id = int(sys.argv[2])

    trace_collection = babeltrace.TraceCollection()
    trace_collection.add_trace(trace_path, 'ctf')

    # Sort events by thread ID.
    thread_event_map = {}
    last_ts = None
    for event in trace_collection.events:
        thread_id = event["gtid"]
        # HACK: Use our converter function to get around event copying bug.
        event_converted = event_as_dict(event)
        # If this thread id hasn't been seen before, add it to the dictionary.
        if thread_event_map.get(thread_id) is None:
            thread_event_map[thread_id] = []
        # Add event to the per-thread event list.
        thread_event_map[thread_id].append(event_converted)

    # Add the duration tags to every thread's events.
    for k in thread_event_map:
        thread_event_map[k] = add_durations(thread_event_map[k])

    # Generate each thread's statistics per-region.
    thread_stats = {k: [] for k in thread_event_map.keys()}
    for k in thread_stats:
        thread_stats[k] = gen_stats(thread_event_map[k])
    #pprint(thread_stats)  # DEBUG

    # Output CSV column headers.
    header = [
        "thread_id",
        "total execute time",
        "total overhead",
        "total energy",
    ]

    # Write results to stdout.
    csvwriter = csv.writer(sys.stdout, delimiter=',')
    csvwriter.writerow(header)
    for thread_id in thread_event_map.keys():
        total_exec, total_overhead, total_energy = thread_stats[thread_id]
        row = [thread_id, total_exec, total_overhead, total_energy]
        csvwriter.writerow(row)
