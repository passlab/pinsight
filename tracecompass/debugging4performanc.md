## Debugging for performance

### Goals: when a program stops at the breakpoints, the debugger will analyze traces collected and show the tracecompass view. 
Essentially it is the integration of LTTng + tracecompass + debugger (editor)
* Need to make sure lttng is able to collect all traces and dump them for analysis
* Need to improve the debugger to bring the tracecompass view to the PTP perspective. 
* PTP can be used for remote debugging, but we can start with local debugging with standard CDT program and then migrate to PTP. 

### Related work
1. [Implementing Debug behavior in the Eclipse IDE - Tutorial](https://www.vogella.com/tutorials/EclipseDebugFramework/article.html)
