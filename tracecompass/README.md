# TraceCompass resources

### OMPT event visualization

Currently, the XML file provides a way to view OMPT events in TraceCompass.


#### Importing the XML file

To be able to view the state changes in a reasonable way in TraceCompass, we have an XML file that provides a special trace view. To import it into TraceCompass, follow the instructions below:

 1. Open TraceCompass. Reset the perspective to defaults, if needed.
 1. Right click on `Project Explorer > Traces`
 1. Select `Manage XML Analyses`.
 1. Select `Import`, and find the `visuomp-analysis.xml` file.
 1. Make sure the box is checked next to the new analysis.
 1. Select `Apply and Close`.
 1. You're done!


### Tracecompass development

 Tracecompass is an Eclipse plugin and framework. Follow the steps from the wiki page for developing with it:
 * Set up Eclipse development environment.
 * Clone [TraceCompass repo](https://git.eclipse.org/c/tracecompass/org.eclipse.tracecompass.git/about/).
 * Study the developer guide and browse the source code.
 * [Eclipse plugin tutorial](http://www.vogella.com/tutorials/EclipsePlugin/article.html).


