# OpenMP OMPT Event Tracing and Visualization

 * lttng tracing framework (http://lttng.org/) for tracing OpenMP runtime using lttng-ust library
 * eclipse-based tracecompass GUI (http://tracecompass.org/) for visualize the tracing results. https://github.com/lttng/lttng-scope and https://github.com/lttng/lttng-analyses, http://diamon.org/babeltrace/
 * It requires to use data-driven analysis of tracecompass to visualize the OpenMP tracing data (http://help.eclipse.org/).
 * Tracecompass developer and user guide (https://wiki.eclipse.org/Trace_Compass)
 * Research project from Polytechnique Montreal: http://hsdm.dorsal.polymtl.ca/
 * Geneviève Bastien's tracing blog: http://versatic.net/
 * https://github.com/efficios that has https://github.com/efficios/babeltrace and other interesting code repo
 * LTTng Scope is a trace viewer and analyzer for CTF traces, with a focus on LTTng kernel and user space traces: https://github.com/lttng/lttng-scope
 
 ### Tracecompass development. 
 Tracecompass is an Eclipse plugin and framework. Follow the steps from the wiki page for the development:
 * Set up Eclipse development environment
 * Clone tracecompass repo (https://git.eclipse.org/c/tracecompass/org.eclipse.tracecompass.git/about/)
 * Study the developer guide and browse source code for the development
 * Eclipse plugin tutorial (http://www.vogella.com/tutorials/EclipsePlugin/article.html)

 ### Screenshot
 #### Lulesh tracing and visualization with Tracecompass
 ![Lulesh tracing and visualization with Tracecompass](OMPT_LTTng_TraceCompass.png). The picture was created in Tracecompass using [Data driven analysis](
 http://archive.eclipse.org/tracecompass/doc/stable/org.eclipse.tracecompass.doc.user/Data-driven-analysis.html#Data_driven_analysis). You need to load visuomp-analysis.xml description file (which describe how to visulize the trace data) in Tracecompass and select the review for that. 
 
