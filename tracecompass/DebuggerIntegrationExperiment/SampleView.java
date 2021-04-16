package pinsightui.views;


import org.eclipse.jface.action.IStatusLineManager;
//import org.eclipse.linuxtools.tmf.core.event.ITmfTimestamp;
//import org.eclipse.linuxtools.tmf.core.event.TmfTimestamp;
import org.eclipse.swt.SWT;
import org.eclipse.swt.widgets.Canvas;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Event;
import org.eclipse.ui.IEditorPart;
import org.eclipse.ui.part.ViewPart;
import org.swtchart.Chart;
import org.swtchart.IAxis;
import org.swtchart.ILineSeries;
import org.swtchart.ISeries.SeriesType;
import org.swtchart.Range;
import org.eclipse.tracecompass.*;
import org.eclipse.tracecompass.tmf.core.event.ITmfEvent;
import org.eclipse.tracecompass.tmf.core.event.ITmfEventField;
import org.eclipse.tracecompass.tmf.core.event.TmfEvent;
import org.eclipse.tracecompass.tmf.core.request.ITmfEventRequest;
import org.eclipse.tracecompass.tmf.core.request.TmfEventRequest;
import org.eclipse.tracecompass.tmf.core.signal.TmfSignalHandler;
import org.eclipse.tracecompass.tmf.core.signal.TmfTimestampFormatUpdateSignal;
import org.eclipse.tracecompass.tmf.core.signal.TmfTraceSelectedSignal;
import org.eclipse.tracecompass.tmf.core.timestamp.TmfTimeRange;
import org.eclipse.tracecompass.tmf.core.timestamp.ITmfTimestamp;
import org.eclipse.tracecompass.tmf.core.timestamp.TmfTimestamp;
import org.eclipse.tracecompass.tmf.core.timestamp.TmfTimestampFormat;
import org.eclipse.tracecompass.tmf.core.trace.ITmfTrace;
import org.eclipse.tracecompass.tmf.core.trace.TmfTraceManager;
import org.eclipse.tracecompass.tmf.ui.editors.ITmfTraceEditor;
import org.eclipse.tracecompass.tmf.ui.signal.TmfUiSignalThrottler;
import org.eclipse.tracecompass.tmf.ui.views.TmfView;
import org.eclipse.tracecompass.tmf.ui.widgets.timegraph.widgets.ITimeDataProvider;
import org.eclipse.tracecompass.tmf.ui.widgets.timegraph.widgets.TimeGraphScale;
import org.eclipse.ui.IEditorPart;
import org.eclipse.tracecompass.tmf.core.signal.TmfSelectionRangeUpdatedSignal;
import org.eclipse.tracecompass.tmf.core.signal.TmfSignal;
import org.eclipse.tracecompass.tmf.ui.views.FormatTimeUtils;
import org.eclipse.tracecompass.tmf.ui.views.FormatTimeUtils.Resolution;
import org.eclipse.tracecompass.tmf.ui.views.FormatTimeUtils.TimeFormat;
import org.eclipse.tracecompass.tmf.ui.views.TmfChartView;
import org.eclipse.swt.SWT;
import java.util.Objects;
import org.eclipse.swt.events.MouseEvent;
import org.eclipse.swt.events.MouseListener;
import org.eclipse.swt.events.MouseMoveListener;
import org.eclipse.swt.events.MouseTrackListener;
import org.eclipse.swt.events.MouseWheelListener;
import org.eclipse.swt.graphics.Color;
import org.eclipse.swt.graphics.GC;
import org.eclipse.tracecompass.tmf.ui.viewers.TmfTimeViewer;
import org.eclipse.tracecompass.tmf.core.signal.TmfWindowRangeUpdatedSignal;
import org.eclipse.tracecompass.tmf.ui.views.histogram.*;
import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.swt.widgets.Event;
import org.eclipse.swt.widgets.Listener;
import org.eclipse.swt.widgets.Shell;
import org.swtchart.Chart;
import org.swtchart.IAxis;
import org.swtchart.ILineSeries;
import org.swtchart.ISeries.SeriesType;

import java.text.FieldPosition;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;




public class SampleView extends TmfView{

	private static final String SERIES_NAME = "Series";
    private static final String Y_AXIS_TITLE = "Threads Number";
    private static final String X_AXIS_TITLE = "Time";
    private static final String VIEW_ID = "pinsight.ui.view";
    private Chart chart;
    private ITmfTrace currentTrace;
    ArrayList<Double> xValues = new ArrayList<Double>();
    ArrayList<Double> yValues = new ArrayList<Double>();
    private double maxY = -Double.MAX_VALUE;
    private double minY = Double.MAX_VALUE;
    private double maxX = -Double.MAX_VALUE;
    private double minX = Double.MAX_VALUE;
    private Canvas fcanvas;
    private static long startX;
    private static long startY;
    private final TmfUiSignalThrottler fTimeSyncThrottle;
    private final TmfUiSignalThrottler fTimeRangeSyncThrottle;

    private static int startXPos;
    private static int startYPos;

    private static long currentX;
    private static long currentY;
    private static boolean drag = false;


    public SampleView() {
		super(VIEW_ID);
		fTimeSyncThrottle = new TmfUiSignalThrottler(this, 200);
	    fTimeRangeSyncThrottle = new TmfUiSignalThrottler(this, 200);
    }
    @Override
    public void createPartControl(Composite parent) {

        chart = new Chart(parent, SWT.BORDER);
        chart.getTitle().setVisible(false);
        chart.getAxisSet().getXAxis(0).getTitle().setText(X_AXIS_TITLE);
        chart.getAxisSet().getYAxis(0).getTitle().setText(Y_AXIS_TITLE);
        chart.getSeriesSet().createSeries(SeriesType.LINE, SERIES_NAME);
        chart.getLegend().setVisible(false);
        final Composite plotArea = chart.getPlotArea();
        plotArea.addListener(SWT.MouseDown, new Listener() {
        	 @Override
             public void handleEvent(Event event) {

        		 IAxis xAxis = chart.getAxisSet().getXAxis(0);
                 IAxis yAxis = chart.getAxisSet().getYAxis(0);

                 startX = (long)xAxis.getDataCoordinate(event.x);
                 startY = (long)yAxis.getDataCoordinate(event.y);
                 
                 //cast back to long int value                

                 startXPos = event.x;
                 startYPos = event.y;
                 System.out.println(startX + " " + startY);
                 System.out.println("selected");
                 drag = true;
        	 }
        });
        
        plotArea.addListener(SWT.MouseUp, new Listener() {

            @Override
            public void handleEvent(Event event) {
                IAxis xAxis = chart.getAxisSet().getXAxis(0);
                IAxis yAxis = chart.getAxisSet().getYAxis(0);

                long endX = (long)xAxis.getDataCoordinate(event.x);
                long endY = (long)yAxis.getDataCoordinate(event.y);

                System.out.println(startX + " " + endX);
                System.out.println(startY + " " + endY);

                drag = false;

                plotArea.redraw();
                updateSelectionTime(startX,endX);
            }
        });
        
        plotArea.addListener(SWT.MouseMove, new Listener() {

            @Override
            public void handleEvent(Event event) {
                if(drag)
                {
                    currentX = event.x;
                    currentY = event.y;

                    plotArea.redraw();
                }
            }
        });
        
        plotArea.addListener(SWT.Paint, new Listener() {

            @Override
            public void handleEvent(Event event) {
                if(drag)
                {
                    GC gc = event.gc;

                    gc.setBackground(Display.getDefault().getSystemColor(SWT.COLOR_BLUE));
                    gc.setAlpha(128);

                    int minX = (int)Math.min(startXPos, currentX);
                    int minY = (int)Math.min(startYPos, currentY);

                    int maxX = (int)Math.max(startXPos, currentX);
                    int maxY = (int)Math.max(startYPos, currentY);

                    int width = maxX - minX;
                    int height = maxY - minY;

                    gc.fillRectangle(minX, minY, width, height);
                }
            }
        });
        
        ITmfTrace trace = TmfTraceManager.getInstance().getActiveTrace();
        if (trace != null) {
            traceSelected(new TmfTraceSelectedSignal(this, trace));
        }else {
            System.out.println("load fail"); 
        }
        chart.getAxisSet().getXAxis(0).getTick().setFormat(new TmfChartTimeStampFormat());
        
    }
    @Override
    public void setFocus() {
        chart.setFocus();
    }
    @TmfSignalHandler
    public void traceSelected(final TmfTraceSelectedSignal signal) {
        // Don't populate the view again if we're already showing this trace
        if (currentTrace == signal.getTrace()) {
            return;
        }
        currentTrace = signal.getTrace();

        // Create the request to get data from the trace

        TmfEventRequest req = new TmfEventRequest(TmfEvent.class,
                TmfTimeRange.ETERNITY, 0, ITmfEventRequest.ALL_DATA,
                ITmfEventRequest.ExecutionType.BACKGROUND) {

            ArrayList<Double> xValues = new ArrayList<Double>();
            ArrayList<Double> yValues = new ArrayList<Double>();

            @Override
            public void handleData(ITmfEvent data) {
                // Called for each event
                super.handleData(data);
                ITmfEventField field = data.getContent().getField();
                if (field != null) {
                    Double yValue = (Double) field.getValue();
                    String fieldString = field.toString();
                    String[] contentSplit = fieldString.split("\\s*,\\s*");
                    for(int i = 0; i < contentSplit.length; i++) {
                    	if (contentSplit[i].contains("global_thread_num")) {
                    		yValue = (double) (contentSplit[i].charAt(contentSplit[i].length()-1)-'0');
                    		break;
                    	}
                    }
                    minY = Math.min(minY, yValue);
                    maxY = Math.max(maxY, yValue);
                    yValues.add(yValue);

                    double xValue = (double) data.getTimestamp().getValue();
                    xValues.add(xValue);
                    minX = Math.min(minX, xValue);
                    maxX = Math.max(maxX, xValue);
                }
            }

            @Override
            public void handleSuccess() {
                // Request successful, not more data available
            	super.handleSuccess();
                final double x[] = toArray(xValues);
                final double y[] = toArray(yValues);

                // This part needs to run on the UI thread since it updates the chart SWT control
                Display.getDefault().asyncExec(new Runnable() {

                    @Override
                    public void run() {
                        chart.getSeriesSet().getSeries()[0].setXSeries(x);
                        chart.getSeriesSet().getSeries()[0].setYSeries(y);

                        // Set the new range
                        
                        chart.getAxisSet().adjustRange();
                        
                        //add mouse event
                        
                        chart.redraw();
                        
                    }
                });
            }

            /**
             * Convert List<Double> to double[]
             */
            private double[] toArray(List<Double> list) {
                double[] d = new double[list.size()];
                for (int i = 0; i < list.size(); ++i) {
                    d[i] = list.get(i);
                }

                return d;
            }
            @Override
            public void handleFailure() {
                // Request failed, not more data available
                super.handleFailure();
            }
            
        };
        ITmfTrace trace = signal.getTrace();
        trace.sendRequest(req);
    }
    public class TmfChartTimeStampFormat extends SimpleDateFormat {
        private static final long serialVersionUID = 1L;
        @Override
        public StringBuffer format(Date date, StringBuffer toAppendTo, FieldPosition fieldPosition) {
            long time = date.getTime();
            toAppendTo.append(TmfTimestampFormat.getDefaulTimeFormat().format(time));
            return toAppendTo;
        }
    }

    @TmfSignalHandler
    public void timestampFormatUpdated(TmfTimestampFormatUpdateSignal signal) {
        // Called when the time stamp preference is changed
        chart.getAxisSet().getXAxis(0).getTick().setFormat(new TmfChartTimeStampFormat());
        chart.redraw();
    }
    
    void updateSelectionTime(long beginTime, long endTime) {
        ITmfTimestamp beginTs = TmfTimestamp.fromNanos(beginTime);
        ITmfTimestamp endTs = TmfTimestamp.fromNanos(endTime);
        TmfSelectionRangeUpdatedSignal signal = new TmfSelectionRangeUpdatedSignal(this, beginTs, endTs, currentTrace);
        fTimeSyncThrottle.queue(signal);
    }
    
    void updateTimeRange(long startTime, long endTime) {
        if (currentTrace != null) {
            // Build the new time range; keep the current time
            TmfTimeRange timeRange = new TmfTimeRange(
                    TmfTimestamp.fromNanos(startTime),
                    TmfTimestamp.fromNanos(endTime));

            // Send the FW signal
            TmfWindowRangeUpdatedSignal signal = new TmfWindowRangeUpdatedSignal(this, timeRange, currentTrace);
            fTimeRangeSyncThrottle.queue(signal);
        }
    }
   
}
