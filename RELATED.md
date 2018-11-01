# Related work

*This is a collection of related work that can be useful as a reference during development.*

 * LTTng tracing framework (http://lttng.org/) for tracing OpenMP runtime using `lttng-ust` library.
 * Eclipse-based tracecompass GUI (http://tracecompass.org/) for visualize the tracing results. https://github.com/lttng/lttng-scope and https://github.com/lttng/lttng-analyses, http://diamon.org/babeltrace/
 * It requires to use data-driven analysis of tracecompass to visualize the OpenMP tracing data (http://help.eclipse.org/).
 * Tracecompass developer and user guide (https://wiki.eclipse.org/Trace_Compass)
 * Research project from Polytechnique Montreal: http://hsdm.dorsal.polymtl.ca/
 * Geneviève Bastien's tracing blog: http://versatic.net/
 * https://github.com/efficios that has https://github.com/efficios/babeltrace and other interesting code repo
 * LTTng Scope is a trace viewer and analyzer for CTF traces, with a focus on LTTng kernel and user space traces: https://github.com/lttng/lttng-scope
 * Vampir :: Vampir is a visualization framework for visualizing OTF format traces collected by the VampirTrace tool.
   [ [LLNL User docs] ](https://hpc.llnl.gov/software/development-environment-software/vampir-vampir-server)
   [ [Vampir low-level docs] ](https://computing.llnl.gov/code/vgv.html)
   [ [SC'17 Presentation] ](https://www.openmp.org/wp-content/uploads/SC17-Tschueter_openmp_booth_talk.pdf)


## Implementation notes

 * We use https://github.com/llvm-mirror/openmp/blob/master/runtime/test/ompt/callback.h as reference to make sure our use of OMPT is up-to-date with the LLVM OpenMP implementation.


## Other projects

 * Graingraph for OpenMP (https://dl.acm.org/citation.cfm?id=2851156)
 * TAU instrument tool (http://www.cs.uoregon.edu/research/tau/home.php)
 * HPCToolkit/Perf (sampling) (http://hpctoolkit.org/)
 * https://www.vi-hps.org/projects/score-p/
 
