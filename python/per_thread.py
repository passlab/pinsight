# Per-thread whole-program CSV generator script.
# Copyright (c) 2018, PASSLab Team. All rights reserved.

# We will need to update these imports when babeltrace 2.x comes out.
import babeltrace
import sys
import csv
import copy
from argparse import ArgumentParser
from pprint import pprint
from multiprocessing import Pool


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


def get_energy(event):
    package_names = ["pkg_energy" + str(i) for i in range(0,4)]
    return [event[x] for x in package_names]


def compute_energy_diff(event_cur, event_prev):
    return sum([(cur - prev) for (cur, prev) in
                zip(get_energy(event_cur), get_energy(event_prev))])


def get_rollover_status(event_cur, event_prev):
    out = []
    # Hardware counters can be rolling over independently of each other.
    # Have to check them independently.
    for (cur, prev) in zip(get_energy(event_cur), get_energy(event_prev)):
        out.append(cur < prev)
    return out


def track_rollovers(rollover_counter, event_cur, event_prev):
    rollover_happened = get_rollover_status(event_cur, event_prev)
    if any(rollover_happened):
        for i in range(0, len(rollover_happened)):
            # Increment the counter(s) of any rollovers.
            if rollover_happened[i]:
                rollover_counter[i] += 1
    return rollover_counter


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
    yield prev
    return prev


# The function that actually generates per-thread statistics.
def gen_stats(events, max_energy_uj=0):
    total_exec_time = 0
    total_overhead = 0
    total_energy = 0
    rollover_counter = {i: 0 for i in range(0, 4)}  # More than 1 HW rollover can happen.

    start_event = next(events)
    end_event = start_event

    if start_event["name"] not in work_event_names:
        total_overhead += start_event["duration"]

    # Figure out total overhead time by grinding through events linearly.
    for event in events:
        if event["name"] not in work_event_names:
            total_overhead += event["duration"]
        rollover_counter = track_rollovers(rollover_counter, event, end_event)
        end_event = event

    # Figure out total execution time by timestamp differences.
    total_exec_time = end_event["timestamp"] - start_event["timestamp"]

    # Figure out energy differences per-package and sum.
    start_energy = get_energy(start_event)
    end_energy = get_energy(end_event)
    # Add rollover counter values to end_energy as needed.
    for i in range(0, 4):
        end_energy[i] += (rollover_counter[i] * max_energy_uj)
    for i in range(0, 4):
        total_energy += end_energy[i] - start_energy[i]

    return [total_exec_time, total_overhead, total_energy, rollover_counter]


if __name__ == "__main__":
    # Command line interface parser.
    parser = ArgumentParser(description=description)

    # Optional arguments.
    parser.add_argument("-j", type=int,
                            action="store",
                            dest="num_procs",
                            default=1,
                            help="Number of parallel processes to use.")

    parser.add_argument("--master", type=int,
                        action="store",
                        dest="master_thread_id",
                        default=0,
                        help="ID of the master thread.")

    parser.add_argument("--max-energy-uj", "-e", type=int,
                        action="store",
                        dest="max_energy_uj",
                        default=0,
                        help="Max energy_uj value. Used to fix hardware counter rollover issues.")

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

    thread_stats = {k: {} for k in threads_in_trace}

    # Process per-thread event streams in parallel.
    work_queue = threads_in_trace
    def process_events(k):
        traces = get_trace_collection(trace_path)  # Rebuild the trace collection.
        stats = gen_stats(thread_events(k, traces.events), args.max_energy_uj)
        return (k, stats)

    eprint("Info: Launching {} processes to process {} threads' data.".format(args.num_procs, args.THREADS_IN_TRACE))
    with Pool(args.num_procs) as pool:
        results = pool.map(process_events, work_queue)
        for (k, stats) in results:
            thread_stats[k] = stats

    # Output CSV column headers.
    header = [
        "thread_id",
        "total_execute_time",
        "total_overhead",
        "total_energy",
        "rollovers_per_pkg",
    ]

    # Write results to stdout.
    csvwriter = csv.writer(sys.stdout, delimiter=',')
    csvwriter.writerow(header)
    for thread_id in thread_stats:
        total_exec, total_overhead, total_energy, rollovers = thread_stats[thread_id]
        row = [thread_id, total_exec, total_overhead, total_energy, rollovers]
        csvwriter.writerow(row)
