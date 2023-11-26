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
 * [Set up Eclipse development environment](https://wiki.eclipse.org/Trace_Compass/Development_Environment_Setup)
 * Clone [TraceCompass repo](https://git.eclipse.org/c/tracecompass/org.eclipse.tracecompass.git/about/).
 * Study the developer guide and browse the source code.
 * [Eclipse plugin tutorial](http://www.vogella.com/tutorials/EclipsePlugin/article.html).

 TraceCompass user guide and developer guide provide details about how to visualize more of the trace data. The doc are from https://wiki.eclipse.org/Trace_Compass and there are more info can be found linking from the main website https://www.eclipse.org/tracecompass/ 

The current visualization is based on Eclipse data-driven analysis (http://archive.eclipse.org/tracecompass/doc/stable/org.eclipse.tracecompass.doc.user/Data-driven-analysis.html#Data_driven_analysis) and The XML file (https://github.com/passlab/pinsight/blob/master/tracecompass/pinsight-analysis.xml) that is loaded to tracecompass will enable tracecompass to auto-visualize the trace data. We would like to enhance tracecompass to have more visualization. To get started:
1. read the full userguide and play the tracecompass GUI interface to get familiar with the functionality and features of tracecompass
2. understand data driven analysis by studying the doc, the XML file and how this is used for the trace output from lttng (https://github.com/passlab/pinsight/blob/master/src/lttng_tracepoint.h). 
3. The current XML only specifies the state provider part of the data driven analysis, I would like you to enhance the analysis and visualization using XML pattern provider, XML time graph view, and XML XY chat. 
4. With the XML approach, we only need to specify how we want the data to be visualized and it has limited features of analysis that needs to aggregate and process trace data. So after you are familiar with XML-based analysis and visualization, you can check the developer guide (http://archive.eclipse.org/tracecompass/doc/stable/org.eclipse.tracecompass.doc.dev/Developer-Guide.html) for this when you are ready. 
5. Research and development at [École Polytechnique de Montréal](https://www.dorsal.polymtl.ca/en/) and there are lots of useful traces and XML file to look at from https://secretaire.dorsal.polymtl.ca/~gbastien/

Another development work we need is to add MPI tracing by using MPI tool interface. 
MPI tool interface has the same mechanisms with OMPT, which is based on event/callback (https://computation.llnl.gov/projects/mpi_t), so it should be fairly straight forward to add in. The challenge will be how we integrate with LTTng since we will need to have multiple LTTng instances, e.g. one per process for collecting tracing. 

## Information for pattern-based data-driven analysis using Trace Compass
1. [Pattern provider from Trace Compass User Guide -- Data-Driven Analysis](https://archive.eclipse.org/tracecompass/doc/stable/org.eclipse.tracecompass.doc.user/Data-driven-analysis.html#Writing_the_XML_pattern_provider)
1. Paper [A declarative framework for stateful analysis of execution traces](https://publications.polymtl.ca/2987/1/2017_Wininger_Declarative_framework_stateful_analysis_execution.pdf)
1. [Gene's XML tracing examples](https://secretaire.dorsal.polymtl.ca/~gbastien/Xml4Traces/)
1. [Analyzing runtime CoreCLR events from Linux – Trace Compass](http://tooslowexception.com/analyzing-runtime-coreclr-events-from-linux-trace-compass/)
1. [One email in Trace Compass maillist](https://www.eclipse.org/lists/tracecompass-dev/msg01199.html)
1. [Geneviève Bastien's tracing blog About "The tale of Trace Compass and the Generic Callstack"](http://www.versatic.net/tracecompass/incubator/callstack/2017/11/27/tale-generic-callstack.html)
1. [Trace Visualization Labs](https://github.com/tuxology/tracevizlab)
1. [Trace Compass Past, Present and Future](https://www.eclipsecon.org/sites/default/files/slides/EclipseConEurope2018-Talk.pdf)

## Tracecompass incubator project
1. kernel callstack: https://archive.eclipse.org/tracecompass.incubator/doc/org.eclipse.tracecompass.incubator.kernel.doc.user/User-Guide.html
1. scripting: https://archive.eclipse.org/tracecompass.incubator/doc/org.eclipse.tracecompass.incubator.scripting.doc.user/User-Guide.html
1. Blog: [Trace Compass Scripting: Empowering Users With Their Trace Data Analysis](http://versatic.net/tracecompass/introducingEase.html)

## Using JavaFX in tracecompass view and for JavaFX 3D visualization

SampleView extends TmfView which extends Eclipse UI ViewPart. TMFview provides mechanisms to access trace data. 
Adding JavaFX to tracecompass view is by the FXCanvas class. Below are the relevant links. You should be able to have some idea how to creating a SWT plugin that access traces via tracecompass. 
* https://docs.oracle.com/javafx/2/api/javafx/embed/swt/FXCanvas.html
* http://blog.vogella.com/2019/11/15/add-javafx-controls-to-a-swt-eclipse-4-application-eclipse-rcp-cookbook-update/
* https://wiki.eclipse.org/Efxclipse/Tutorials/Tutorial2 (this could be the starting point)
* https://docs.oracle.com/javafx/2/swt_interoperability/jfxpub-swt_interoperability.htm
* http://www.java2s.com/Code/Java/JavaFX/JavaFXSWTIntegration.htm
* Eclipse has a project for integrating JavaFX https://www.eclipse.org/efxclipse/index.html where you can find the Tutorial2 above. 
* More website that may be helpful:https://blog.codecentric.de/en/2015/04/add-javafx-controls-swt-eclipse-4-application-eclipse-rcp-cookbook/
* https://github.com/sxfszwr/JavaFx-SWT
* https://www.eclipse.org/swt/
* https://github.com/tracecompass/tracecompass/blob/master/tmf/org.eclipse.tracecompass.tmf.ui/src/org/eclipse/tracecompass/tmf/ui/views/TmfView.java
* https://github.com/eclipse/eclipse.platform.ui/blob/master/bundles/org.eclipse.ui.workbench/Eclipse%20UI/org/eclipse/ui/part/ViewPart.java
* Check the tracecompass developer guide, there is a section about component integration. With some source code of existing tracecompass view, you should be able to figure out how to let the view respond to the mouse event and react accordingly in other component. https://archive.eclipse.org/tracecompass/doc/org.eclipse.tracecompass.doc.dev/Component-Interaction.html#Component_Interaction
* https://github.com/sshahriyar/org.eclipse.tracecompass/blob/master/org.eclipse.tracecompass.tmf.ui/src/org/eclipse/tracecompass/tmf/ui/views/TmfView.java
the broadcast function is for sending the signal to other components via the an object of TmfTraceManager. That will trigger highlighting of trace records in other components of the interface. 


## To degvelopment 3D visualization in tracecompass using existing SWT
Tracecompass uses Eclipse [SWTChart](http://www.swtchart.org/index.html) to create 2D chart. SWTChart uses [Eclipse SWT](https://www.eclipse.org/swt/) for widget. While there are Java library for creating 3D graphs, e.g. https://jogamp.org/, http://www.jzy3d.org/, Java 3D and [others](https://en.wikipedia.org/wiki/List_of_3D_graphics_libraries), the easiest for us is probably to extend SWT or SWTChart with OpenGL. The reason is that it will be easier integrate with Eclipse control and other library including Tracecompass and the way it handles CTF traces, which would yield high performance implementation. [The Tracecompass tutorial in the developer guide](https://help.eclipse.org/luna/index.jsp?topic=%2Forg.eclipse.linuxtools.tmf.help%2Fdoc%2FView-Tutorial.html) provides a starting point. 

To use OpenGL in Eclipse SWT and SWTChart, we should read [SWT with OpenGL](https://www.eclipse.org/swt/opengl/) and [Using OpenGL with SWT(old, but very helpful)](https://www.eclipse.org/articles/Article-SWT-OpenGL/opengl.html). The article "[Graphics Context - Quick on the draw](https://www.eclipse.org/articles/Article-SWT-graphics/SWT_graphics.html)" provides basic background about drawing in SWT. 

Our implementation only needs to support chart type, just like SWTChart, so it should be easier to do a general 3D visualization project. 

### TODO items
1. Follow the [The Tracecompass tutorial in the developer guide](https://help.eclipse.org/luna/index.jsp?topic=%2Forg.eclipse.linuxtools.tmf.help%2Fdoc%2FView-Tutorial.html) to create a sample view plugin using our trace data. The tutorial is based on older version of Eclipse. For the latest Eclipse, you have to manually edit SampleView class to extend TmfView and then make those changes of the source code. Source codes for Tracecompass's own views can be used as reference when you add source code: https://github.com/tracecompass/tracecompass/tree/master/tmf/org.eclipse.tracecompass.tmf.ui/src/org/eclipse/tracecompass/tmf/ui/views. [TmfChartView.java](https://github.com/tracecompass/tracecompass/blob/master/tmf/org.eclipse.tracecompass.tmf.ui/src/org/eclipse/tracecompass/tmf/ui/views/TmfChartView.java) file is a good place to start

1. Explore adding OpenGL support in SWTChart, this could be done as standalone SWTChart experiment or in the sample view plugin.


### Related work
[Graphical Editing Framework 3D project](https://wiki.eclipse.org/GEF3D) seems to be dead since there is no activities for years. It is designed to enabling editing capability for 2D and 3D images in Eclipse. For [Eclipse Advanced Visualization Project](https://projects.eclipse.org/proposals/eclipse-advanced-visualization-project) led by ORNL, it is inactive for years. The attempt was to integrate HPC visualization tools (visiIt, paraView, etc) with Eclipse, thus can be considered as loose integration with Eclipse. It hard to enable control and interaction between the image rendered by 3rd party tools with the data presented in Eclipse. 

With regards to Eclipse SWT, Oracle/Java has AWT, SWing, JavaFx and Java3D. But it seems those Java/Oracle-provided implementation are heavyweight while Eclipse SWT is lightweight. More research and study are needed if we need to know more detail. 

