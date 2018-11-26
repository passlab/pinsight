# Per-thread per-region CSV generator script.
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


# Reverse a codeptr-to-parallel_id-list mapping.
def reverse_mapping(mapping: dict):
    out = {}
    for k, v in mapping.items():
        for parallel_id in v:
            out[parallel_id] = k
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
def gen_stats(events: list, codeptr_map: dict):
    # Output variables.
    region_type = {k: 0 for k in codeptr_map}
    region_num_runs = {k: 0 for k in codeptr_map}
    region_total_exec_time = {k: 0 for k in codeptr_map}
    region_total_overhead = {k: 0 for k in codeptr_map}
    region_total_energy = {k: 0 for k in codeptr_map}
    # Local state-tracking and helper variables.
    region_stack = []
    mapping = reverse_mapping(codeptr_map)
    package_names = ["pkg_energy" + str(i) for i in range(0,4)]
    gtid = events[0]["gtid"]  # DEBUG: helper variable for context.
    seen = set()
    # Iterate across events.
    for event in events:
        parallel_id = event.get("parallel_id")
        if parallel_id is not None:
            # Ensure consistent codeptr value.
            codeptr_ra = mapping[parallel_id]
            seen.add(codeptr_ra)
            ts_cur = event["timestamp"]

            # Push state on begin event.
            if event["name"].endswith("_begin") and codeptr_ra != 0:
                if event["name"] == "lttng_pinsight:implicit_task_begin":
                    event_type = event["name"][:-6]  # Name - "_begin". Used for prefix matching.
                    region_type[codeptr_ra] = event_type
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
    out = {
        k: {
            "type": region_type[k],
            "num_runs": region_num_runs[k],
            "total_exec_time": region_total_exec_time[k],
            "total_overhead": region_total_overhead[k],
            "total_energy": region_total_energy[k],
        } for k in list(seen)
    }
    return out


if __name__ == "__main__":
    # Check for right number of arguments.
    if len(sys.argv) < 3:
        usage = "Usage:\n  python3 per_thread_per_region.py path/to/trace MASTER_THREAD_ID"
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
    thread_stats = {k: {} for k in thread_event_map.keys()}
    codeptr_map = gen_codeptr_map(thread_event_map[master_thread_id])
    for k in thread_stats:
        thread_stats[k] = gen_stats(thread_event_map[k], codeptr_map)
    #pprint(thread_stats)  # DEBUG

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
    for thread_id in thread_event_map.keys():
        stats = thread_stats[thread_id]
        for codeptr_ra in stats:
            num_runs = stats[codeptr_ra]["num_runs"]
            total_exec = stats[codeptr_ra]["total_exec_time"]
            total_overhead = stats[codeptr_ra]["total_overhead"]
            total_energy = stats[codeptr_ra]["total_energy"]
            row = [thread_id, "0x{:02X}".format(codeptr_ra), num_runs, total_exec, total_overhead, total_energy]
            csvwriter.writerow(row)
