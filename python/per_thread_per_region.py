# Per-thread per-region CSV generator script.
# Copyright (c) 2018, PASSLab Team. All rights reserved.

# We will need to update these imports when babeltrace 2.x comes out.
import babeltrace
import sys
import csv
import copy
from pprint import pprint
from collections import defaultdict


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


# Reverse a codeptr-to-parallel_id-list mapping.
def reverse_mapping(mapping: dict):
    out = {}
    for k, v in mapping.items():
        for parallel_id in v:
            out[parallel_id] = k
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


# Use on master thread to generate mapping between `codeptr_ra` values and parallel region ids.
def gen_codeptr_map(events: list):
    out = {}
    for event in events:
        codeptr_ra = event.get("codeptr_ra")
        parallel_id = event.get("parallel_id")
        if codeptr_ra is not None:
            if codeptr_ra in out:
                out[codeptr_ra].add(parallel_id)
            else:
                out[codeptr_ra] = set([parallel_id])
    return out


# The function that actually generates per-region statistics.
def gen_stats(events: dict, codeptr_to_paridset=defaultdict(set)):
    # Output variables.
    region_num_runs = defaultdict(int)
    region_total_exec_time = defaultdict(int)
    region_total_overhead = defaultdict(int)
    region_total_energy = defaultdict(int)
    # Local state-tracking and helper variables.
    region_stack = []
    mapping = reverse_mapping(codeptr_to_paridset)
    package_names = ["pkg_energy" + str(i) for i in range(0,4)]
    #gtid = events[0]["gtid"]  # DEBUG: helper variable for context.
    seen = set()
    # Iterate across events.
    for event in events:
        codeptr_ra = event.get("codeptr_ra")
        parallel_id = event.get("parallel_id")
        if codeptr_ra is not None and parallel_id is not None:
            # Ensure we're not looking at an "_end" event, and it's not a "work_single" event.
            if codeptr_ra != 0 and parallel_id != 0 and "single" not in event["name"]:
                codeptr_to_paridset[codeptr_ra].add(parallel_id)
                mapping[parallel_id] = codeptr_ra
        if parallel_id is not None:
            # Ensure consistent codeptr value.
            if parallel_id == 0 and len(region_stack) > 0:
                parallel_id = region_stack[-1][2]
            codeptr_ra = mapping[parallel_id]
            seen.add(codeptr_ra)
            ts_cur = event["timestamp"]

            # Push state on begin event.
            if event["name"].endswith("_begin") and codeptr_ra != 0:
                if event["name"] == "lttng_pinsight:implicit_task_begin":
                    event_type = event["name"][:-6]  # Name - "_begin". Used for prefix matching.
                    region_stack.append([event_type, codeptr_ra, parallel_id, event["timestamp"]] + [event[x] for x in package_names])

            # Mark event as overhead if it's not a work-type event.
            if (event["name"] not in work_event_names and len(region_stack) > 0):
                region_total_overhead[codeptr_ra] += event["duration"]

            # Pop state to generate measurements.
            if (event["name"] == "lttng_pinsight:implicit_task_end"):
                event_type, codeptr_ra_prev, parallel_id_prev, ts_prev, *energy_prev = region_stack.pop()
                # Get correct codeptr_ra and parallel_id
                parallel_id = parallel_id_prev
                codeptr_ra = codeptr_ra_prev
                region_num_runs[codeptr_ra] += 1
                # Validation check.
                if codeptr_ra != codeptr_ra_prev and event["name"][:-4] != "lttng_pinsight:implicit_task":
                    eprint("Error: Thread {}, Region beginning at {:02X} does not match region end at {:02X}.".format(gtid, codeptr_ra, codeptr_ra_prev))
                # Use prev values to figure out what happened over the interval.
                duration = ts_cur - ts_prev
                assert(duration > 0)
                energy_cur = [event[x] for x in package_names]
                energy = [(cur - prev) for (cur, prev) in zip(energy_cur, energy_prev)]
                # Update global region stat trackers.
                region_total_exec_time[codeptr_ra] += duration
                region_total_energy[codeptr_ra] += sum(energy)
        # Implicit else: ignore non parallel region events.
    #pprint(region_stack)  # DEBUG

    # Corrects accounting on num_runs when last trace records are dropped.
    if len(region_stack) > 0:
        for item in region_stack:
            eprint("Warning: Correcting number of runs for unfinished region. {}".format(item))
            codeptr_ra = item[1]
            region_num_runs[codeptr_ra] += 1

    out = {
        k: {
            "num_runs": region_num_runs[k],
            "total_exec_time": region_total_exec_time[k],
            "total_overhead": region_total_overhead[k],
            "total_energy": region_total_energy[k],
        } for k in list(seen)
    }
    return out, codeptr_to_paridset


if __name__ == "__main__":
    # Check for right number of arguments.
    if len(sys.argv) < 4:
        usage = "Usage:\n  python3 per_thread_per_region.py path/to/trace NUM_TRACE_THREADS MASTER_THREAD_ID"
        eprint("Error: Too few arguments.\n"+usage)
        exit(1)
 
    # Get the trace path from the first command line argument.
    trace_path = sys.argv[1]
    threads_in_trace = list(range(0, int(sys.argv[2])))
    master_thread_id = int(sys.argv[3])

    # Sort events by thread ID.
    last_ts = None
    thread_stats = {k: {} for k in threads_in_trace}

    # Build parallel region map off of master thread.
    # Note: O(N) runtime; goes through all events once.
    traces = get_trace_collection(trace_path)
    # Chain generators together.
    codeptr_to_paridset = defaultdict(set)
    thread_stats[master_thread_id], codeptr_to_paridset = gen_stats(thread_events(master_thread_id, traces.events), codeptr_to_paridset)

    # TODO: Try doing this in parallel using the `multiprocessing` module.
    for k in thread_stats:
        # Skip the master thread; we just processed it.
        if k == master_thread_id:
            continue
        # Do the work for everybody else, using the stuff from master.
        traces = get_trace_collection(trace_path)  # Rebuild the trace collection.
        thread_stats[k], _ = gen_stats(thread_events(k, traces.events), codeptr_to_paridset)

    # Output CSV column headers.
    header = [
        "thread_id",
        "codeptr_ra",
        "num_runs",
        "total_execute_time",
        "total_overhead",
        "total_energy",
    ]

    # Write results to stdout.
    csvwriter = csv.writer(sys.stdout, delimiter=',')
    csvwriter.writerow(header)
    for thread_id in thread_stats:
        stats = thread_stats[thread_id]
        for codeptr_ra in stats:
            num_runs = stats[codeptr_ra]["num_runs"]
            total_exec = stats[codeptr_ra]["total_exec_time"]
            total_overhead = stats[codeptr_ra]["total_overhead"]
            total_energy = stats[codeptr_ra]["total_energy"]
            row = [thread_id, "0x{:02X}".format(codeptr_ra), num_runs, total_exec, total_overhead, total_energy]
            csvwriter.writerow(row)
