# Per-thread per-region CSV generator script.
# Copyright (c) 2018, PASSLab Team. All rights reserved.

# We will need to update these imports when babeltrace 2.x comes out.
import babeltrace
import sys
import csv
import copy
from argparse import ArgumentParser
from pprint import pprint
from collections import defaultdict
from multiprocessing import Pool


#---------------------------------------------------------
# Global variables

events_of_interest = frozenset([
    "lttng_pinsight:thread_begin",
    "lttng_pinsight:thread_end",
    "lttng_pinsight:parallel_begin",
    "lttng_pinsight:parallel_end",
    "lttng_pinsight:implicit_task_begin",
    "lttng_pinsight:implicit_task_end",
    "lttng_pinsight:barrier_wait_begin",
    "lttng_pinsight:barrier_wait_end",
])

description = "Per-thread per-region CSV generator script."


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


def get_energy(event):
    package_names = ["pkg_energy" + str(i) for i in range(0,4)]
    return [event[x] for x in package_names]


def compute_energy_diff(event_cur, event_prev):
    return sum([(cur - prev) for (cur, prev) in
                zip(get_energy(event_cur), get_energy(event_prev))])


def verify_event_type(event, event_type):
    if event["name"] != event_type:
        eprint("Warning: Expected event of type {}, got event of type {} instead. {}".format(event_type, event["name"], event))


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
def gen_stats(events: dict, codeptr_to_paridset=defaultdict(set), splits={}):
    # Output variables.
    region_num_runs = defaultdict(int)

    region_total_exec_time = defaultdict(int)
    region_total_overhead = defaultdict(int)
    region_total_idle = defaultdict(int)  # Time spent idling after region completes.

    region_energy_total = defaultdict(int)
    region_energy_overhead = defaultdict(int)
    region_energy_idle = defaultdict(int)

    # Local state-tracking and helper variables.
    is_master = False
    master_last_barrier = None  # Timestamp of last barrier master saw. (Used for idle accounting.)
    region_stack = []
    stack_parallel = []  # parallel_begin/end
    stack_implicit = []  # implicit_task_begin/end
    stack_barrier = []   # barrier_wait_begin/end
    mapping = reverse_mapping(codeptr_to_paridset)
    #gtid = events[0]["gtid"]  # DEBUG: helper variable for context.
    seen = set()
    # Iterate across events.
    for event in events:
        # Skip events we don't care about.
        if event["name"] not in events_of_interest:
            continue
        # Process events.
        if   event["name"] == "lttng_pinsight:parallel_begin":
            is_master = True  # Useful later for master-specific overhead accounting.
            # Set mapping variables (this is the only place we should do this).
            codeptr_ra = event["codeptr_ra"]
            parallel_id = event["parallel_id"]
            codeptr_to_paridset[codeptr_ra].add(parallel_id)
            mapping[parallel_id] = codeptr_ra
            #stack_parallel.append(event)
            region_stack.append(event)
        elif event["name"] == "lttng_pinsight:implicit_task_begin":
            #stack_implicit.append(event)
            region_stack.append(event)
        elif event["name"] == "lttng_pinsight:barrier_wait_begin":
            #stack_barrier.append(event)
            region_stack.append(event)
        # Master-specific tracking/measuring.
        elif event["name"] == "lttng_pinsight:parallel_end":
            #prev = stack_parallel.pop()
            prev = region_stack.pop()
            #verify_event_type(prev, "lttng_pinsight:parallel_begin")
            assert(prev["name"] == "lttng_pinsight:parallel_begin")
            codeptr_ra = prev["codeptr_ra"]
            parallel_id = prev["parallel_id"]
            seen.add(codeptr_ra)
            splits[parallel_id] = event["timestamp"]  # Save where the overhead/idle breakover point is.
            # Master's idle will be brief, and happens betwen barrier_wait_end and parallel_end.
            region_total_idle[codeptr_ra] += event["timestamp"] - master_last_barrier
            region_energy_idle[codeptr_ra] += compute_energy_diff(event, prev)
        # Do majority of measurement.
        elif event["name"] == "lttng_pinsight:implicit_task_end":
            #prev = stack_implicit.pop()
            prev = region_stack.pop()
            assert(prev["name"] == "lttng_pinsight:implicit_task_begin")
            #verify_event_type(prev, "lttng_pinsight:implicit_task_begin")
            parallel_id = prev["parallel_id"]
            codeptr_ra = mapping[parallel_id]
            seen.add(codeptr_ra)
            region_num_runs[codeptr_ra] += 1
            # TODO: Actually measure performance and energy.
            # Measure `total_exec_time` here, along with total energy burned.
            region_total_exec_time[codeptr_ra] += event["timestamp"] - prev["timestamp"]
            region_energy_total[codeptr_ra] += compute_energy_diff(event, prev)
        # Do overhead/idle tracking here.
        elif event["name"] == "lttng_pinsight:barrier_wait_end":
            #prev = stack_barrier.pop()
            prev = region_stack.pop()
            #verify_event_type(prev, "lttng_pinsight:barrier_wait_begin")
            assert(prev["name"] == "lttng_pinsight:barrier_wait_begin")
            parallel_id = prev["parallel_id"]
            codeptr_ra = mapping[parallel_id]
            seen.add(codeptr_ra)
            if is_master:
                master_last_barrier = event["timestamp"]
                region_total_overhead[codeptr_ra] += event["timestamp"] - prev["timestamp"]
                region_energy_overhead[codeptr_ra] += compute_energy_diff(event, prev)
                # Note: Idle time calculated in parallel_end for master.
            else:
                total_time = event["timestamp"] - prev["timestamp"]
                # Is this a "split barrier" that needs special processing?
                if splits[parallel_id] > prev["timestamp"]:
                    # For workers: master parallel_end splits overhead vs idle for last barrier.
                    overhead_time = splits[parallel_id] - prev["timestamp"]
                    idle_time     = event["timestamp"]  - splits[parallel_id]
                    region_total_overhead[codeptr_ra] += overhead_time
                    region_total_idle[codeptr_ra]     += idle_time
                    # Note: Energy measurement is tricky here because we have to split the measurement.
                    energy = compute_energy_diff(event, prev)
                    region_energy_overhead[codeptr_ra] += int((overhead_time / total_time) * energy)
                    region_energy_idle[codeptr_ra]     += int((idle_time / total_time) * energy)
                else:
                    # Normal case. (Pure overheads.)
                    region_total_overhead[codeptr_ra] += total_time
                    region_energy_overhead[codeptr_ra] += compute_energy_diff(event, prev)
        else:
            pass
            #eprint(event)  # DEBUG

    # Corrects accounting on num_runs when last trace records are dropped.
    if len(region_stack) > 0:
        for item in region_stack:
            eprint("Warning: Correcting number of runs for unfinished region. {}".format(item))
            codeptr_ra = mapping[item["parallel_id"]]
            region_num_runs[codeptr_ra] += 1

    out = {
        k: {
            "num_runs": region_num_runs[k],
            "total_exec_time": region_total_exec_time[k],
            "total_overhead": region_total_overhead[k],
            "total_idle": region_total_idle[k],
            "energy_total": region_energy_total[k],
            "energy_overhead": region_energy_overhead[k],
            "energy_idle": region_energy_idle[k],
        } for k in list(seen)
    }
    return out, codeptr_to_paridset, splits


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

    eprint("Info: Processing master thread.")
    # Build parallel region map off of master thread.
    # Note: O(N) runtime; goes through all events once.
    traces = get_trace_collection(trace_path)
    # Chain generators together.
    codeptr_to_paridset = defaultdict(set)
    splits = {}
    thread_stats[master_thread_id], codeptr_to_paridset, splits = gen_stats(thread_events(master_thread_id, traces.events), codeptr_to_paridset, splits)

    # Process per-thread event streams in parallel.
    work_queue = []
    for k in thread_stats:
        # Skip the master thread; we just processed it.
        if k == master_thread_id:
            continue
        # Do the work for everybody else, using the stuff from master.
        work_queue.append(k)

    def process_events(k):
        traces = get_trace_collection(trace_path)  # Rebuild the trace collection.
        stats, _, _ = gen_stats(thread_events(k, traces.events), codeptr_to_paridset, splits)
        return (k, stats)

    eprint("Info: Launching {} processes to process {} threads' data.".format(args.num_procs, args.THREADS_IN_TRACE - 1))
    with Pool(args.num_procs) as pool:
        results = pool.map(process_events, work_queue)
        for (k, stats) in results:
            thread_stats[k] = stats

    # Output CSV column headers.
    header = [
        "thread_id",
        "codeptr_ra",
        "num_runs",
        "total_execute_time",
        "total_overhead",
        "total_idle",
        "energy_total",
        "energy_overhead",
        "energy_idle",
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
            total_idle = stats[codeptr_ra]["total_idle"]
            energy_total = stats[codeptr_ra]["energy_total"]
            energy_overhead = stats[codeptr_ra]["energy_overhead"]
            energy_idle = stats[codeptr_ra]["energy_idle"]
            row = [thread_id, "0x{:02X}".format(codeptr_ra), num_runs, total_exec, total_overhead, total_idle, energy_total, energy_overhead, energy_idle]
            csvwriter.writerow(row)
