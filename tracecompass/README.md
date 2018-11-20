# TraceCompass resources

### OMPT event visualization

Currently, the XML file provides a way to view OMPT events in TraceCompass.


#### Importing the XML file

To be able to view the state changes in a reasonable way in TraceCompass, we have an XML file that provides a special trace view. To import it into TraceCompass, follow the instructions below:

 1. Open TraceCompass. Reset the perspective to defaults, if needed.
 1. Right click on `Project Explorer > Traces`
 1. Select `Manage XML Analyses`.
 1. Select `Import`, and find the `pinsight-analysis.xml` file.
 1. Make sure the box is checked next to the new analysis.
 1. Select `Apply and Close`.
 1. You're done!

-----

### Tracecompass development

*For those interested in further development with TraceCompass, the following resources may be useful.*

 Tracecompass is an Eclipse plugin and framework. Follow the steps from the wiki page for developing with it:
 * Set up Eclipse development environment.
 * Clone [TraceCompass repo](https://git.eclipse.org/c/tracecompass/org.eclipse.tracecompass.git/about/).
 * Study the developer guide and browse the source code.
 * [Eclipse plugin tutorial](http://www.vogella.com/tutorials/EclipsePlugin/article.html).

TraceCompass user guide and developer guide provide details about how to visualize more of the trace data. The doc are from https://wiki.eclipse.org/Trace_Compass and there are more info can be found linking from the main website https://www.eclipse.org/tracecompass/ 

The current visualization is based on Eclipse data-driven analysis (http://archive.eclipse.org/tracecompass/doc/stable/org.eclipse.tracecompass.doc.user/Data-driven-analysis.html#Data_driven_analysis) and The XML file (https://github.com/passlab/pinsight/blob/master/tracecompass/pinsight-analysis.xml) that is loaded to tracecompass will enable tracecompass to auto-visualize the trace data. We would like to enhance tracecompass to have more visualization. To get started:
1. read the full userguide and play the tracecompass GUI interface to get familiar with the functionality and features of tracecompass
2. understand data driven analysis by studying the doc, the XML file and how this is used for the trace output from lttng (https://github.com/passlab/pinsight/blob/master/src/lttng_tracepoint.h). 
3. The current XML only specifies the state provider part of the data driven analysis, I would like you to enhance the analysis and visualization using XML pattern provider, XML time graph view, and XML XY chat. 
4. With the XML approach, we only need to specify how we want the data to be visualized and it has limited features of analysis that needs to aggregate and process trace data. So after you are familiar with XML-based analysis and visualization, you can check the developer guide (http://archive.eclipse.org/tracecompass/doc/stable/org.eclipse.tracecompass.doc.dev/Developer-Guide.html) for this when you are ready. 

Another development work we need is to add MPI tracing by using MPI tool interface. 
MPI tool interface has the same mechanisms with OMPT, which is based on event/callback (https://computation.llnl.gov/projects/mpi_t), so it should be fairly straight forward to add in. The challenge will be how we integrate with LTTng since we will need to have multiple LTTng instances, e.g. one per process for collecting tracing. 

