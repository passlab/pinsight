## Instructions for starting developing Eclipse/Tracecompass-based visualization for PInsight
This folder contains instructions and sample files/traces to help one start developing Eclipse/Tracecompass-based visualization

### Set up tracecompass and visualize the sample data using XML data-driven view. 
1. Download and install tracecompass (https://www.eclipse.org/tracecompass/). 
2. Read [tracecompass user guide](https://archive.eclipse.org/tracecompass/doc/stable/org.eclipse.tracecompass.doc.user/User-Guide.html) to get familiar with the UI interface and their functionality
3. Important the attached xml file pinsight-analysis.xml to setup pinsight data-driven analysis and visualization. The XML file is developed using a [data driven analysis solution in tracecompass](https://archive.eclipse.org/tracecompass/doc/stable/org.eclipse.tracecompass.doc.user/Data-driven-analysis.html#Data_driven_analysis). 
4. Open the attached trace folder to visualize 

Second: creating a plugin in Eclipse1. Check the tutorial for creating a Tracecompass plugin. https://archive.eclipse.org/tracecompass/doc/org.eclipse.tracecompass.doc.dev/View-Tutorial.html#View_Tutorial. The developer guide also includes other features for analysis and visualization (https://archive.eclipse.org/tracecompass/doc/org.eclipse.tracecompass.doc.dev/Developer-Guide.html). 2. Have the example working to the step of creating an empty view, but not using the data provided by the example traces in the tutorial since we will need to populate the view using the trace files attached in this email. The code used in the the tutorial example are outdated and I am requesting some code from another person who did this example before. 3. In your plugin view, visualize the parallel region of the traces by analyzing the lttng_pinsight:parallel_begin and lttng_pinsight:parallel_end events for the trace you have. You can put in the bar chart for the visualization: X-axis is the parallel_codeptr, y-axis is the accumulated execution time of the parallel region. 
For example in the following code, there are two parallel regions, executed 10 and 20 times. Your traces will have a pair parallel_begin and parallle_end events for each execution of a parallel region. Thus in your analysis code, you will iterate all the traces and search the event pair, find the difference of the time-stamp of the two events, which is the execution time for each execution, and the accumulated all the execution time of the same parallel region. a parallel region is identified by parallel_codeptr event filed. get started based on your understanding and let me know if you have any questions. 
main() {  for (i=0; i<10; i++) {      //sequential region      #pragma omp parallel num_threads(4) //parallel region 1, executed 10 times, parallel_begin event      {           printf("hello world, parallel region #1\n");       }   //parallel_end event     //sequential region    }
  for (i=0; i<20; i++) {       //sequential region      #pragma omp parallel num_threads(6) //parallel region 2, executed 20 times      {                       printf("hello world, parallel region #2\n");       }      //sequential region  }
}

The following are steps Yaying gave for debugging the plugin under Eclipse: 
On Fri, Jun 12, 2020 at 9:36 PM Yaying Shi <yshi10@uncc.edu> wrote:
Dear Professor Yan,
We still need launching the plugin in the Eclipse IDE.
Then process is launch first:
 Then import trace data
 
Then add plugin in the Eclipse.
 