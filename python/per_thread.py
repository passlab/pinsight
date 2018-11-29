# Per-thread whole-program CSV generator script.
# Copyright (c) 2018, PASSLab Team. All rights reserved.

# We will need to update these imports when babeltrace 2.x comes out.
import babeltrace
import sys
import csv
import copy
from argparse import ArgumentParser
from pprint import pprint


#---------------------------------------------------------
# Global variables

work_event_names = [
    "lttng_pinsight:parallel_begin",
    "lttng_pinsight:implicit_task_begin",
    "lttng_pinsight:work_begin",
]

description = "Per-thread (whole program) CSV generator script."


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


# Create a new TraceCollection object (required for new events iterator).
def get_trace_collection(trace_path):
    trace_collection = babeltrace.TraceCollection()
    trace_collection.add_traces_recursive(trace_path, 'ctf')
    return trace_collection


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


# Generator function that allows filtering events from the events
# iterator by their `gtid` fields. Also adds `duration` tags.
# events: babeltrace.TraceCollection.events generator
def thread_events(thread_id: int, events):
    prev = None
    # Queue up first event in the stream.
    for event in events:
        if event["gtid"] == thread_id:
            prev = event_as_dict(event)
            break
    # For all events thereafter, add on the duration field, and yield.
    for event in events:
        if event["gtid"] == thread_id:
            prev["duration"] = event.timestamp - prev["timestamp"]
            yield prev
            prev = event_as_dict(event)
    # HACK: Return last event with hard-coded duration.
    prev["duration"] = 1
    return prev


# The function that actually generates per-thread statistics.
def gen_stats(events):
    total_exec_time = 0
    total_overhead = 0
    total_energy = 0

    start_event = next(events)
    end_event = None

    if start_event["name"] not in work_event_names:
        total_overhead += start_event["duration"]

    # Figure out total overhead time by grinding through events linearly.
    for event in events:
        if event["name"] not in work_event_names:
            total_overhead += event["duration"]
        end_event = event

    # Figure out total execution time by timestamp differences.
    total_exec_time = end_event["timestamp"] - start_event["timestamp"]

    # Figure out energy differences per-package and sum.
    package_names = ["pkg_energy" + str(i) for i in range(0,4)]
    start_energy = [start_event[x] for x in package_names]
    end_energy = [end_event[x] for x in package_names]
    for i in range(0,4):
        total_energy += end_energy[i] - start_energy[i]

    return [total_exec_time, total_overhead, total_energy]


if __name__ == "__main__":
    # Command line interface parser.
    parser = ArgumentParser(description=description)

    # Optional arguments.
    parser.add_argument("-j", type=int,
                            action="store",
                            dest="num_procs",
                            help="Number of parallel processes to use.")

    parser.add_argument("--master", type=int,
                        action="store",
                        dest="master_thread_id",
                        default=0,
                        help="ID of the master thread.")

    # Required arguments.
    parser.add_argument("TRACE_PATH", type=str,
                        action="store",
                        help="Path to the LTTng trace data.")

    parser.add_argument("THREADS_IN_TRACE", type=int,
                        action="store",
                        help="Number of threads in LTTng trace data.")

    args = parser.parse_args()

    # Use command line arguments.
    trace_path = args.TRACE_PATH
    threads_in_trace = list(range(0, int(args.THREADS_IN_TRACE)))
    master_thread_id = args.master_thread_id

    # TODO: Try doing this in parallel using the `multiprocessing` module.
    thread_stats = {k: {} for k in threads_in_trace}
    for k in thread_stats:
        traces = get_trace_collection(trace_path)  # Rebuild the trace collection.
        thread_stats[k] = gen_stats(thread_events(k, traces.events))

    # Output CSV column headers.
    header = [
        "thread_id",
        "total_execute_time",
        "total_overhead",
        "total_energy",
    ]

    # Write results to stdout.
    csvwriter = csv.writer(sys.stdout, delimiter=',')
    csvwriter.writerow(header)
    for thread_id in thread_stats:
        total_exec, total_overhead, total_energy = thread_stats[thread_id]
        row = [thread_id, total_exec, total_overhead, total_energy]
        csvwriter.writerow(row)
