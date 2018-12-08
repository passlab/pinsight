# Energy plotting script.
# Copyright (c) 2018, PASSLab Team. All rights reserved.
from bokeh.core.properties import value
from bokeh.io import show, output_file, export_png, export_svgs
from bokeh.models import ColumnDataSource, FactorRange
from bokeh.plotting import figure

import sys
import csv
from collections import defaultdict
from bokeh.core.properties import value
from bokeh.io import show, output_file, export_png, export_svgs
from bokeh.models import ColumnDataSource, FactorRange
from bokeh.plotting import figure
from bokeh.models.annotations import Legend, LegendItem

output_file("energy_plot.html")


if __name__ == "__main__":
    # Check number of command line args.
    if len(sys.argv) < 2:
        print("Error: Not enough arguments.")
        exit(1)

    # Read CSV data in.
    data = defaultdict(list)
    with open(sys.argv[1], 'r') as f:
        csvreader = csv.DictReader(f, delimiter=',')
        for row in csvreader:
            for k in row:
                data[k].append(row[k])
    # Auto-detect if it's per-region or per-thread plotting.
    is_per_region = False
    if "codeptr_ra" in data:
        is_per_region = True

    # Set up plot data sources.
    factors = None
    source = None
    categories = None
    colors = None

    # TODO: Move to (TOTAL_THREADS, BENCHMARK_NAME) formatting.
    # Note; Format is (Thread ID, Region ID)
    if is_per_region:
        factors = [(x, y) for (x, y) in zip(data["thread_id"], data["codeptr_ra"])]
    else:
        factors = data["thread_id"]

    if is_per_region:
        # Per-region data source.
        source = ColumnDataSource(data=dict(
            x=factors,
            num_runs=[int(x) for x in data["num_runs"]],
            work=[(int(t) - int(o)) for (t, o) in zip(data["energy_total"], data["energy_overhead"])],
            overhead=[int(x) for x in data["energy_overhead"]],
            idle=[int(x) for x in data["energy_idle"]],
        ))
    else:
        # Per-thread data source.
        source = ColumnDataSource(data=dict(
            x=factors,
            energy=[int(x) for x in data["energy_total"]],
        ))

    # Unavailable fields will just have ??? instead of values.
    tooltip = [
        ("work", "@work uJ"),
        ("overhead", "@overhead uJ"),
        ("idle", "@idle uJ"),
        ("num_runs", "@num_runs"),
        ("energy", "@energy uJ"),
    ]

    p = figure(x_range=FactorRange(*factors),
               plot_height=500,
               toolbar_location="above",
               tooltips=tooltip
               )  # tools="")

    # Actual plotting.
    if is_per_region:
        categories = ['work', 'overhead', 'idle']
        colors = ['blue', 'red', 'orange']

        r_work, r_overhead, r_idle = p.vbar_stack(categories, x='x', width=0.9, alpha=0.5, color=colors, source=source)#,
                 #legend=[value(x) for x in categories])

        legend = Legend(items=[
            ("overhead" , [r_overhead]),
            ("work" , [r_work]),
            ("idle" , [r_idle]),
        ], location=(10, -30))
    else:
        print("Not implemented yet.")
        exit(1)

    p.add_layout(legend, 'right')

    p.yaxis.axis_label = "Energy (uJ)"
    if is_per_region:
        p.xaxis.axis_label = "Thread ID / Region ID"
    else:
        p.xaxis.axis_label = "Thread ID"
    p.y_range.start = 0
    p.y_range.end = max([int(x) for x in data["energy_total"]])
    p.x_range.range_padding = 0.1
    p.xaxis.major_label_orientation = 1
    p.xgrid.grid_line_color = None
    #p.legend.location = "top_center"
    #p.legend.orientation = "vertical"

    show(p)

    #export_png(p, "out.png")
    #p.output_backend = "svg"
    #export_svgs(p, filename="out.svg")
