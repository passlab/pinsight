package pinsight3dfxview.views;


import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.ui.part.*;
import org.eclipse.jface.viewers.*;
import org.eclipse.swt.graphics.Image;
import org.eclipse.jface.action.*;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.ui.*;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.tracecompass.tmf.core.event.ITmfEvent;
import org.eclipse.tracecompass.tmf.core.event.ITmfEventField;
import org.eclipse.tracecompass.tmf.core.event.TmfEvent;
import org.eclipse.tracecompass.tmf.core.request.ITmfEventRequest;
import org.eclipse.tracecompass.tmf.core.request.TmfEventRequest;
import org.eclipse.tracecompass.tmf.core.signal.TmfSignalHandler;
import org.eclipse.tracecompass.tmf.core.signal.TmfTraceSelectedSignal;
import org.eclipse.tracecompass.tmf.core.timestamp.TmfTimeRange;
import org.eclipse.tracecompass.tmf.core.trace.ITmfTrace;
import org.eclipse.tracecompass.tmf.core.trace.TmfTraceManager;
import org.eclipse.swt.SWT;
import javax.inject.Inject;

//import org.eclipse.fx.ui.workbench3.FXViewPart;
import javafx.embed.swt.FXCanvas;
import javafx.scene.Scene;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;

import javafx.event.EventHandler;
import javafx.scene.Group;
import javafx.scene.PerspectiveCamera;
import javafx.scene.SceneAntialiasing;
import javafx.scene.input.ScrollEvent;
import javafx.scene.layout.Pane;
import javafx.scene.layout.StackPane;
import javafx.scene.paint.Color;
import javafx.scene.paint.Paint;
import javafx.scene.paint.PhongMaterial;
import javafx.scene.shape.Box;
import javafx.scene.shape.Line;
import javafx.scene.shape.Rectangle;
import javafx.scene.transform.Rotate;


public class Pinsight3DView extends ViewPart {

	/**
	 * The ID of the view as specified by the extension.
	 */
	public static final String ID = "pinsight3dfxview.views.Pinsight3DView";
	private FXCanvas canvas;
	private ITmfTrace currentTrace;
	int size = 400;
	private static Random rnd = new Random();
    private double mousePosX, mousePosY;
    private double mouseOldX, mouseOldY;
    private final Rotate rotateX = new Rotate(20, Rotate.X_AXIS);
    private final Rotate rotateY = new Rotate(-45, Rotate.Y_AXIS);

	@Override
	public void createPartControl(Composite parent) {
		this.canvas = new FXCanvas(parent, SWT.NONE);
		ITmfTrace trace = TmfTraceManager.getInstance().getActiveTrace();
        if (trace != null) {
            traceSelected(new TmfTraceSelectedSignal(this, trace));
        }else {
            System.out.println("load fail"); 
        }
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
            ArrayList<String> zValues = new ArrayList<String>();
            ArrayList<Double> xValuesE = new ArrayList<Double>();
            ArrayList<Double> yValuesE = new ArrayList<Double>();
            ArrayList<String> zValuesE = new ArrayList<String>();

            @Override
            public void handleData(ITmfEvent data) {
                // Called for each event
                super.handleData(data);
                ITmfEventField field = data.getContent().getField();
                String Eventname = data.getName();
                if (Eventname.equals("ompt_pinsight_lttng_ust:implicit_task_begin") && field != null) {
                    Double yValue = (Double) field.getValue();
                    String zValue;
                    String fieldString = field.toString();
                    String[] contentSplit = fieldString.split("\\s*,\\s*");
                    for(int i = 0; i < contentSplit.length; i++) {
                    	if (contentSplit[i].contains("global_thread_num")) {
                    		yValue = (double) (contentSplit[i].charAt(contentSplit[i].length()-1)-'0');
                    	}else if(contentSplit[i].contains("parallel_codeptr")) {
                    		String[] contentSplit1 = contentSplit[i].split("\\=");
                    		zValue = contentSplit1[1];
                    		zValues.add(zValue);
                    	}
                    }
                    yValues.add(yValue);
                    double xValue = (double) data.getTimestamp().getValue();
                    xValues.add(xValue);
                }else if (Eventname.equals("ompt_pinsight_lttng_ust:implicit_task_end") && field != null) {
                	Double yValue = (Double) field.getValue();
                    String zValue;
                    String fieldString = field.toString();
                    String[] contentSplit = fieldString.split("\\s*,\\s*");
                    for(int i = 0; i < contentSplit.length; i++) {
                        if (contentSplit[i].contains("omp_thread_num")) {
                    		yValue = (double) (contentSplit[i].charAt(contentSplit[i].length()-1)-'0');
                    	}else if(contentSplit[i].contains("parallel_codeptr")) {
                    		String[] contentSplit1 = contentSplit[i].split("\\=");
                    		zValue = contentSplit1[1];
                    		zValuesE.add(zValue);
                    	}
                    }
                    yValuesE.add(yValue);
                    double xValue = (double) data.getTimestamp().getValue();
                    xValuesE.add(xValue);
                }
            }

            @Override
            public void handleSuccess() {
                // Request successful, not more data available
            	super.handleSuccess();
                final double x[] = toArray(xValues);
                final double y[] = toArray(yValues);
                Object[] temp= zValues.toArray();
                String[] z = Arrays.copyOf(temp, temp.length, String[].class);
                final double xE[] = toArray(xValuesE);
                final double yE[] = toArray(yValuesE);
                Object[] temp1= zValuesE.toArray();
                String[] zE = Arrays.copyOf(temp1, temp1.length, String[].class);

                // This part needs to run on the UI thread since it updates the chart SWT control
                Display.getDefault().asyncExec(new Runnable() {

                    @Override
                    public void run() {
                    	Group cube = createCube(size);
                    	cube.getTransforms().addAll(rotateX, rotateY);
                        StackPane root = new StackPane();
                        root.getChildren().add(cube);
                        
                        double gridSizeHalf = size / 2;
                        double size = 30;
                        for (double i = -gridSizeHalf + size; i < gridSizeHalf; i += 50) {
                            for (double j = -gridSizeHalf + size; j < gridSizeHalf; j += 50) {

                                double height =  rnd.nextDouble() * 300;;

                                Box box = new Box(size, height, size);

                                // color
                                PhongMaterial mat = new PhongMaterial();
                                mat.setDiffuseColor(randomColor());
                                box.setMaterial(mat);

                                // location
                                box.setLayoutY(-height * 0.5 + 400 * 0.5);
                                box.setTranslateX(i);
                                box.setTranslateZ(j);

                                cube.getChildren().addAll(box);

                            }
                        }
                        
                        Scene scene = new Scene(root, 1600, 900, true, SceneAntialiasing.BALANCED);
                        scene.setCamera(new PerspectiveCamera());

                        scene.setOnMousePressed(me -> {
                            mouseOldX = me.getSceneX();
                            mouseOldY = me.getSceneY();
                        });
                        scene.setOnMouseDragged(me -> {
                            mousePosX = me.getSceneX();
                            mousePosY = me.getSceneY();
                            rotateX.setAngle(rotateX.getAngle() - (mousePosY - mouseOldY));
                            rotateY.setAngle(rotateY.getAngle() + (mousePosX - mouseOldX));
                            mouseOldX = mousePosX;
                            mouseOldY = mousePosY;
                            lttng_pinsight
                        });

                        makeZoomable(root);
                        canvas.setScene(scene);
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

	@Override
	public void setFocus() {
		canvas.setFocus();
	}
	private Group createCube(int size) {

        Group cube = new Group();

        // size of the cube
        Color color = Color.ALICEBLUE;

        List<Axis> cubeFaces = new ArrayList<>();
        Axis r;

        // back face
        r = new Axis(size);
        r.setFill(color.deriveColor(0.0, 1.0, (1 - 0.5 * 1), 1.0));
        r.setTranslateX(-0.5 * size);
        r.setTranslateY(-0.5 * size);
        r.setTranslateZ(0.5 * size);

        cubeFaces.add(r);

        // bottom face
        r = new Axis(size);
        r.setFill(color.deriveColor(0.0, 1.0, (1 - 0.4 * 1), 1.0));
        r.setTranslateX(-0.5 * size);
        r.setTranslateY(0);
        r.setRotationAxis(Rotate.X_AXIS);
        r.setRotate(90);

        cubeFaces.add(r);

        // right face
        r = new Axis(size);
        r.setFill(color.deriveColor(0.0, 1.0, (1 - 0.3 * 1), 1.0));
        r.setTranslateX(-1 * size);
        r.setTranslateY(-0.5 * size);
        r.setRotationAxis(Rotate.Y_AXIS);
        r.setRotate(90);

        // cubeFaces.add( r);

        // left face
        r = new Axis(size);
        r.setFill(color.deriveColor(0.0, 1.0, (1 - 0.2 * 1), 1.0));
        r.setTranslateX(0);
        r.setTranslateY(-0.5 * size);
        r.setRotationAxis(Rotate.Y_AXIS);
        r.setRotate(90);

        cubeFaces.add(r);

        // top face
        r = new Axis(size);
        r.setFill(color.deriveColor(0.0, 1.0, (1 - 0.1 * 1), 1.0));
        r.setTranslateX(-0.5 * size);
        r.setTranslateY(-1 * size);
        r.setRotationAxis(Rotate.X_AXIS);
        r.setRotate(90);

        // cubeFaces.add( r);

        // front face
        r = new Axis(size);
        r.setFill(color.deriveColor(0.0, 1.0, (1 - 0.1 * 1), 1.0));
        r.setTranslateX(-0.5 * size);
        r.setTranslateY(-0.5 * size);
        r.setTranslateZ(-0.5 * size);

        // cubeFaces.add( r);

        cube.getChildren().addAll(cubeFaces);

        return cube;
    }
	 public static class Axis extends Pane {

	        Rectangle wall;

	        public Axis(double size) {

	            // wall
	            // first the wall, then the lines => overlapping of lines over walls
	            // works
	            wall = new Rectangle(size, size);
	            getChildren().add(wall);

	            // grid
	            double zTranslate = 0;
	            double lineWidth = 1.0;
	            Color gridColor = Color.WHITE;

	            for (int y = 0; y <= size; y += size / 10) {

	                Line line = new Line(0, 0, size, 0);
	                line.setStroke(gridColor);
	                line.setFill(gridColor);
	                line.setTranslateY(y);
	                line.setTranslateZ(zTranslate);
	                line.setStrokeWidth(lineWidth);

	                getChildren().addAll(line);

	            }

	            for (int x = 0; x <= size; x += size / 10) {

	                Line line = new Line(0, 0, 0, size);
	                line.setStroke(gridColor);
	                line.setFill(gridColor);
	                line.setTranslateX(x);
	                line.setTranslateZ(zTranslate);
	                line.setStrokeWidth(lineWidth);TmfView

	                getChildren().addAll(line);

	            }

	            // labels
	            // TODO: for some reason the text makes the wall have an offset
	            // for( int y=0; y <= size; y+=size/10) {
	            //
	            // Text text = new Text( ""+y);
	            // text.setTranslateX(size + 10);
	            //
	            // text.setTranslateY(y);
	            // text.setTranslateZ(zTranslate);
	            //
	            // getChildren().addAll(text);
	            //
	            // }

	        }

	        public void setFill(Paint paint) {
	            wall.setFill(paint);
	        }

	    }
	 public void makeZoomable(StackPane control) {

	        final double MAX_SCALE = 20.0;
	        final double MIN_SCALE = 0.1;

	        control.addEventFilter(ScrollEvent.ANY, new EventHandler<ScrollEvent>() {

	            @Override
	            public void handle(ScrollEvent event) {

	                double delta = 1.2;

	                double scale = control.getScaleX();

	                if (event.getDeltaY() < 0) {
	                    scale /= delta;
	                } else {
	                    scale *= delta;
	                }

	                scale = clamp(scale, MIN_SCALE, MAX_SCALE);

	                control.setScaleX(scale);
	                control.setScaleY(scale);

	                event.consume();

	            }

	        });

	    }
	 public static double clamp(double value, double min, double max) {

	        if (Double.compare(value, min) < 0)
	            return min;

	        if (Double.compare(value, max) > 0)
	            return max;

	        return value;
	    }
	 public static Color randomColor() {
	        return Color.rgb(rnd.nextInt(255), rnd.nextInt(255), rnd.nextInt(255));
	    }
}
