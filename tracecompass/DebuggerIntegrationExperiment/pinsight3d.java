package pinsight3d.views;


import org.eclipse.swt.widgets.Composite;
import org.eclipse.swt.widgets.Display;
import org.eclipse.ui.part.*;


import org.eclipse.jface.viewers.*;
import org.eclipse.swt.graphics.Image;
import org.eclipse.jface.action.*;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.ui.*;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.widgets.Canvas;
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

//import org.eclipse.fx.ui.workbench3.FXViewPart;
import javafx.embed.swt.FXCanvas;
import javafx.scene.Scene;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;
import java.util.HashMap;

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
import javafx.scene.text.Text;
import javafx.scene.input.MouseEvent;


import org.eclipse.ui.part.*;
import org.eclipse.jface.viewers.*;
import org.eclipse.swt.graphics.Image;
import org.eclipse.jface.action.*;
import org.eclipse.jface.dialogs.MessageDialog;
import org.eclipse.ui.*;
import org.eclipse.swt.widgets.Menu;
import org.eclipse.swt.SWT;
import javax.inject.Inject;


/**
 * This sample class demonstrates how to plug-in a new
 * workbench view. The view shows data obtained from the
 * model. The sample creates a dummy model on the fly,
 * but a real implementation would connect to the model
 * available either in this or another plug-in (e.g. the workspace).
 * The view is connected to the model using a content provider.
 * <p>
 * The view uses a label provider to define how model
 * objects should be presented in the view. Each
 * view can present the same model objects using
 * different labels and icons, if needed. Alternatively,
 * a single label provider can be shared between views
 * in order to ensure that objects of the same type are
 * presented in the same way everywhere.
 * <p>
 */

public class pinsight3d extends ViewPart {

	/**
	 * The ID of the view as specified by the extension.
	 */
	public static final String ID = "pinsight3d.views.SampleView";

	private FXCanvas canvas;
	private ITmfTrace currentTrace;
	int size = 1000;
	private static Random rnd = new Random();
    private double mousePosX, mousePosY;
    private double mouseOldX, mouseOldY;
    private final Rotate rotateX = new Rotate(20, Rotate.X_AXIS);
    private final Rotate rotateY = new Rotate(-45, Rotate.Y_AXIS);
    //get the current trace
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

	//when you get the trace, we draw 3D chart
	@TmfSignalHandler
    public void traceSelected(final TmfTraceSelectedSignal signal) {
        // Don't populate the view again if we're already showing this trace
        if (currentTrace == signal.getTrace()) {
            return;
        }
        currentTrace = signal.getTrace();
        System.out.println("I am here");
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
                    		String[] threads = contentSplit[i].split("\\=");
                    		yValue = Double.valueOf(threads[1]);
                    		//yValue = (double) (contentSplit[i].charAt(contentSplit[i].length()-1)-'0');
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
                    	if (contentSplit[i].contains("global_thread_num")) {
                    		String[] threads = contentSplit[i].split("\\=");
                    		yValue = Double.valueOf(threads[1]);
                    		//yValue = (double) (contentSplit[i].charAt(contentSplit[i].length()-1)-'0');
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
                System.out.println(Arrays.toString(x));
                System.out.println(Arrays.toString(y));
                System.out.println(Arrays.toString(z));
                System.out.println(Arrays.toString(xE));
                System.out.println(Arrays.toString(yE));
                System.out.println(Arrays.toString(zE));


                // This part needs to run on the UI thread since it updates the chart SWT control
                Display.getDefault().asyncExec(new Runnable() {

                    @Override
                    public void run() {
                    	Group cube = createCube(size,x,xE,zE,yE);
                    	cube.getTransforms().addAll(rotateX, rotateY);
                        StackPane root = new StackPane();
                        root.getChildren().add(cube);
                        
                        ArrayList<String> dis_regin = new ArrayList<String>();
                        for (int i = 0; i < zE.length; i++) {
                        	if (!dis_regin.contains(zE[i])) {
                        		dis_regin.add(zE[i]);
                        	}
                        }
                        ArrayList<Double> thread = new ArrayList<Double>();
                        for (int i = 0; i < yE.length; i++) {
                        	if (!thread.contains(yE[i])) {
                        		thread.add(yE[i]);
                        	}
                        }
                        System.out.println(Arrays.toString(y));
                        System.out.println(Arrays.toString(thread.toArray()));
                        double bsize = 80;
                        int size = 1000;
                        
                        double time = (xE[xE.length-1] - x[0])/20;
                        for (int i = 0 ; i < y.length; i++) {
                        	for (int j = 0; j< yE.length; j++) {
                        		double lenth;
                        		if (y[i] == yE[j] && z[i].equals(zE[j])) {
                                    //size color mat

                        			lenth = ((xE[j]-x[i])/time)*(size/10);
                        			double start=((x[i]-x[0])/time)*(size/10);
                                    Box box = new Box(bsize,bsize,2*lenth);
                                    //location
                                    box.setTranslateY(0.5*(size/10)*thread.size()- (size/20) - (y[i]*(size/10)));
                                    int ind = dis_regin.indexOf(zE[j]);
                                    //System.out.println(ind);
                                    box.setTranslateX((size/10)*dis_regin.size()-(size/20) - ind * (size/10));
                                    PhongMaterial mat = new PhongMaterial();
                                    Color clo = randomColor();
                                    if ((ind%3) == 0) {
                                    	int col = (int) (255 - (255* y[i]/thread.size())); 
                                    	clo = Color.rgb(255, col, 0);
                                    }else if ((ind%3) == 2) {
                                    	int col = (int) (255 - (255* y[i]/thread.size()));
                                    	clo = Color.rgb(0, 255, col);
                                    }else if ((ind%3) == 1) {
                                    	int col = (int) (255 - (255* y[i]/thread.size()));
                                    	clo = Color.rgb(col, 0, 255);
                                    }
                                    mat.setDiffuseColor(clo);
                                    box.setMaterial(mat);
                                    box.setTranslateZ(size-lenth/2-1-start);
                                    cube.getChildren().addAll(box);
                                    //pop it out from end list
                                    yE[j] = -1;
                                    //System.out.println("running");
                                    break;
                        		}
                        		if (j == yE.length-1) {
                        			lenth = ((xE[xE.length-1]-x[i])/time)*50;
                        			double start=((x[i]-x[0])/time)*50;
                                    Box box = new Box(bsize,bsize,lenth);
                                    //location
                                    box.setTranslateY(0.5*(size/10)*thread.size()- 25 - (y[i]*50));
                                    int ind = dis_regin.indexOf(zE[j]);
                                    System.out.println(ind);
                                    PhongMaterial mat = new PhongMaterial();
                                    Color clo = randomColor();
                                    if (ind == 0) {
                                    	int col = (int) (255 - (255* y[i]/thread.size())); 
                                    	clo = Color.rgb(255, col, 0);
                                    }else if (ind == 2) {
                                    	int col = (int) (255 - (255* y[i]/thread.size()));
                                    	clo = Color.rgb(0, 255, col);
                                    }else if (ind == 1) {
                                    	int col = (int) (255 - (255* y[i]/thread.size()));
                                    	clo = Color.rgb(col, 0, 255);
                                    }
                                    mat.setDiffuseColor(clo);
                                    box.setMaterial(mat);
                                    box.setTranslateX(0.5*(size/10)*dis_regin.size()+ 50 - ind * 50);
                                    box.setTranslateZ(size-lenth/2-1-start);
                                    cube.getChildren().addAll(box);
                                    //pop it out from end list
                                    yE[j] = -1;
                                    System.out.println("running");
                                    break;
                        		}
                        	}
                        }
                        System.out.println("finish");
                        /*
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
                                
                                box.setOnMouseClicked(mouseEvent->{
                                
                                	System.out.println("selected");
                                    
                                });
                                box.addEventFilter(MouseEvent.MOUSE_CLICKED, event -> System.out.println("Clicked!"));

                                cube.getChildren().addAll(box);

                            }
                        }*/
                        cube.addEventHandler(MouseEvent.MOUSE_CLICKED, event -> System.out.println("Cube Clicked!"));
                        Scene scene = new Scene(root, 1600, 900, true, SceneAntialiasing.BALANCED);
                        scene.setCamera(new PerspectiveCamera());

                        scene.setOnMousePressed(me -> {
                            mouseOldX = me.getSceneX();
                            mouseOldY = me.getSceneY();
                            System.out.println("Scene Clicked!");
                        });
                        scene.setOnMouseDragged(me -> {
                            mousePosX = me.getSceneX();
                            mousePosY = me.getSceneY();
                            rotateX.setAngle(rotateX.getAngle() - (mousePosY - mouseOldY));
                            rotateY.setAngle(rotateY.getAngle() + (mousePosX - mouseOldX));
                            mouseOldX = mousePosX;
                            mouseOldY = mousePosY;

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
	private Group createCube(int size,double [] x, double [] xE, String [] zE, double [] yE) {

        Group cube = new Group();

        // size of the cube
        Color color = Color.ALICEBLUE;

        List<Axis> cubeFaces = new ArrayList<>();
        Axis r;
        
        ArrayList<String> dis_regin = new ArrayList<String>();
        for (int i = 0; i < zE.length; i++) {
        	if (!dis_regin.contains(zE[i])) {
        		dis_regin.add(zE[i]);
        	}
        }
        Object[] temp1= dis_regin.toArray();
        String[] region = Arrays.copyOf(temp1, temp1.length, String[].class);
        ArrayList<Double> thread = new ArrayList<Double>();
        for (int i = 0; i < yE.length; i++) {
        	if (!thread.contains(yE[i])) {
        		thread.add(yE[i]);
        	}
        }
        // back face
        r = new Axis(size,dis_regin.size(),thread.size());
        r.setFill(color.deriveColor(0.0, 1.0, (1 - 0.5 * 1), 1.0));
        //r.setTranslateX(-0.5 *(size/10)*20);
        r.setTranslateY(-0.5 *(size/10)*thread.size());
        r.setTranslateZ(0.5 * (size/10)*20);

        //cubeFaces.add(r);

        // bottom face
        r = new Axis(size,dis_regin.size(),20);
        r.setFill(color.deriveColor(0.0, 1.0, (1 - 0.4 * 1), 1.0));
        r.setTranslateX(0);
        r.setTranslateY(-0.5 * (size/10)*20 + 0.5 *(size/10)*thread.size());
        r.setRotationAxis(Rotate.X_AXIS);
        r.setRotate(90);

        cubeFaces.add(r);

        // right face
        //r = new Axis(size);
        //r.setFill(color.deriveColor(0.0, 1.0, (1 - 0.3 * 1), 1.0));
        //r.setTranslateX(-1 * size);
        //r.setTranslateY(-0.5 * size);
        //r.setRotationAxis(Rotate.Y_AXIS);
        //r.setRotate(90);

        // cubeFaces.add( r);

        // left face
        r = new Axis(size,20,thread.size());
        r.setFill(color.deriveColor(0.0, 1.0, (1 - 0.2 * 1), 1.0));
        r.setTranslateX(-0.5 * (size/10)*20 + (size/10)*dis_regin.size());
        r.setTranslateY(-0.5 *(size/10)*thread.size());
        r.setRotationAxis(Rotate.Y_AXIS);
        r.setRotate(90);

        //cubeFaces.add(r);

        // top face
        //r = new Axis(size);
        //r.setFill(color.deriveColor(0.0, 1.0, (1 - 0.1 * 1), 1.0));
        //r.setTranslateX(-0.5 * size);
        //r.setTranslateY(-1 * size);
        //r.setRotationAxis(Rotate.X_AXIS);
        //r.setRotate(90);

        // cubeFaces.add( r);

        // front face
        //r = new Axis(size);
        //r.setFill(color.deriveColor(0.0, 1.0, (1 - 0.1 * 1), 1.0));
        //r.setTranslateX(-0.5 * size);
        //r.setTranslateY(-0.5 * size);
        //r.setTranslateZ(-0.5 * size);

        // cubeFaces.add( r);

        cube.getChildren().addAll(cubeFaces);
        
        /*
        double gridSizeHalf = size / 2;
        double labelOffset = 10 ;
        double labelPos = - gridSizeHalf - labelOffset ;

        for (double coord = -gridSizeHalf ; coord < gridSizeHalf ; coord+=50) {
            Text xLabel = new Text(coord, -labelPos, String.format("%.0f", coord));
            xLabel.setTranslateZ(labelPos);
            xLabel.setScaleY(-1);
            Text yLabel = new Text(labelPos, -coord, String.format("%.0f", coord));
            yLabel.setTranslateZ(-labelPos);
            yLabel.setScaleX(-1);
            Text zLabel = new Text(labelPos, -labelPos, String.format("%.0f", coord));
            zLabel.setTranslateZ(coord);
            cube.getChildren().addAll(xLabel, yLabel, zLabel);
            zLabel.setScaleX(-1);
        }*/
        
        //cube y axis in left side
        int i = 0;
        
        for( int y= (size/10)*thread.size() + size/10; y > 0; y-=size/10) {
            
        	if (y == (size/10)*thread.size() + size/10) {
        		Text text = new Text( "Threads Number");
                text.setTranslateX((size/10)*dis_regin.size());
                text.setTranslateY(-0.5 *(size/10)*thread.size());
                text.setTranslateZ(-size - 50);
                text.setRotationAxis(Rotate.Y_AXIS);
                text.setRotate(90);
                cube.getChildren().addAll(text);
        	}else {
        		Text text = new Text( String.valueOf(i));
                text.setTranslateX((size/10)*dis_regin.size());
                text.setTranslateY(-0.5 *(size/10)*thread.size()+y -25);
                text.setTranslateZ(-size -50);
                text.setRotationAxis(Rotate.Y_AXIS);
                text.setRotate(90);
                i += 1;
                cube.getChildren().addAll(text);
        	}  
           
            }
        //cube y axis in right side
        i = 0;
        for( int y= (size/10)*thread.size() + size/10; y > 0; y-=size/10) {
            
        	if (y == (size/10)*thread.size() + size/10) {
        		Text text = new Text( "Threads Number");
                text.setTranslateX(-size/10);
                text.setTranslateY(-0.5 *(size/10)*thread.size());
                text.setTranslateZ(size+50);
                text.setRotationAxis(Rotate.Y_AXIS);
                text.setRotate(90);
                cube.getChildren().addAll(text);
        	}else {
        		Text text = new Text( String.valueOf(i));
                text.setTranslateX(-size/10);
                text.setTranslateY(-0.5 *(size/10)*thread.size()+y -25);
                text.setTranslateZ(size-50);
                text.setRotationAxis(Rotate.Y_AXIS);
                text.setRotate(90);
                i += 1;
                cube.getChildren().addAll(text);
        	}           
        }

      //cube Y axis in right view side
        
         i = 0;
        for(int y=(size/10)*dis_regin.size() + size/10; y > 0; y-=size/10) {
            
        	if (y == (size/10)*dis_regin.size() + size/10) {
        		Text text = new Text( "Parallel Region");
                text.setTranslateX(-size/10);
                text.setTranslateY(0.5 *(size/10)*thread.size());
                text.setTranslateZ(size +50);
                //text.setRotationAxis(Rotate.X_AXIS);
                //text.setRotate(-90);
                text.setRotationAxis(Rotate.Y_AXIS);
                text.setRotate(90);
                cube.getChildren().addAll(text);
                
        	}else {
        		Text text = new Text( String.valueOf(region[i]));
                text.setTranslateX( y-size/10);
                text.setTranslateY(0.5 *(size/10)*thread.size());
                text.setTranslateZ(size +50);
                //text.setRotationAxis(Rotate.X_AXIS);
                //text.setRotate(-90);
                text.setRotationAxis(Rotate.Y_AXIS);
                text.setRotate( 90);
                i += 1;
               
                cube.getChildren().addAll(text);
        	} 
           
            }
        
      //cube x axis in normal side
        i = 0;
        for( int y=(size/10)*dis_regin.size() + size/10; y > 0; y-=size/10) {
            
        	if (y == (size/10)*dis_regin.size() + size/10) {
        		Text text = new Text( "Parallel Region");
                text.setTranslateX(-size/10);
                text.setTranslateY(0.5 *(size/10)*thread.size());
                text.setTranslateZ(-size -50);
                //text.setRotationAxis(Rotate.X_AXIS);
                //text.setRotate(-90);
                text.setRotationAxis(Rotate.Y_AXIS);
                text.setRotate(90);
                cube.getChildren().addAll(text);
                
        	}else {
        		Text text = new Text( String.valueOf(region[i]));
                text.setTranslateX( y-size/10);
                text.setTranslateY(0.5 *(size/10)*thread.size());
                text.setTranslateZ(-size -50);
                //text.setRotationAxis(Rotate.X_AXIS);
                //text.setRotate(-90);
                text.setRotationAxis(Rotate.Y_AXIS);
                text.setRotate( 90);
                i += 1;
               
                cube.getChildren().addAll(text);
        	}  
        
            }
      //cube X axis in right view side
        i = 0;
        Double time = xE[xE.length-1] - x[0];
        for(  int y=size * 2 +size/10; y > 0; y-=size/10) {
            
        	if (y == size*2 + size/10) {
        		Text text = new Text( "Time(ms)");
                text.setTranslateX(-size/10);
                text.setTranslateY(0.5 *(size/10)*thread.size()+50);
                text.setTranslateZ(size+50);
                text.setRotationAxis(Rotate.X_AXIS);
                text.setRotate(-90);
                //text.setRotationAxis(Rotate.Y_AXIS);
                //text.setRotate(90);
               
                cube.getChildren().addAll(text);
        	}else {
        		Double time1 = time/20 * i/10000;
        		//System.out.println(Math.round(time1));
        		Text text = new Text( String.valueOf((Math.round(time1))/100.0));
                text.setTranslateX(-size/10);
                text.setTranslateY(0.5 *(size/10)*thread.size()+50);
                text.setTranslateZ(y - (size+25));
                text.setRotationAxis(Rotate.X_AXIS);
                text.setRotate(-90);
                //text.setRotationAxis(Rotate.Y_AXIS);
                //text.setRotate(90);
                i += 1;
                cube.getChildren().addAll(text);
           
            }
        }
      //cube Y axis in normal side
        i = 0;
        for( int y=size * 2 +size/10; y > 0; y-=size/10) {
            
        	if (y == size*2 + size/10) {
        		Text text = new Text( "Time(ms)");
                text.setTranslateX(-2*size/10);
                text.setTranslateY(0.5 *(size/10)*thread.size());
                text.setTranslateZ(size+50);
                text.setRotationAxis(Rotate.X_AXIS);
                text.setRotate(-90);
                text.setRotationAxis(Rotate.Y_AXIS);
                text.setRotate(90);
               
                cube.getChildren().addAll(text);
        	}else {
        		Double time1 = time/20 * i/10000;
        		//System.out.println(Math.round(time1));
        		Text text = new Text( String.valueOf((Math.round(time1))/100.0));
                text.setTranslateX(-2*size/10);
                text.setTranslateY(0.5 *(size/10)*thread.size());
                text.setTranslateZ(y - (size+25));
                text.setRotationAxis(Rotate.X_AXIS);
                text.setRotate(-90);
                text.setRotationAxis(Rotate.Y_AXIS);
                text.setRotate(90);
                i += 1;
                cube.getChildren().addAll(text);
        	}
            
        
            }
        return cube;
    }
	 public static class Axis extends Pane {

	        Rectangle wall;

	        public Axis(double size, double len, double hight) {

	            // wall
	            // first the wall, then the lines => overlapping of lines over walls
	            // works
	            wall = new Rectangle((size/10)*len, (size/10)*hight);
	            getChildren().add(wall);

	            // grid
	            double zTranslate = 0;
	            double lineWidth = 1.0;
	            Color gridColor = Color.WHITE;

	            for (int y = 0; y <= (size/10)*hight; y += size / 10) {

	                Line line = new Line(0, 0, (size/10)*len, 0);
	                line.setStroke(gridColor);
	                line.setFill(gridColor);
	                line.setTranslateY(y);
	                line.setTranslateZ(zTranslate);
	                line.setStrokeWidth(lineWidth);

	                getChildren().addAll(line);

	            }

	            for (int x = 0; x <=  (size/10)*len; x += size / 10) {

	                Line line = new Line(0, 0, 0, (size/10)*hight);
	                line.setStroke(gridColor);
	                line.setFill(gridColor);
	                line.setTranslateX(x);
	                line.setTranslateZ(zTranslate);
	                line.setStrokeWidth(lineWidth);

	                getChildren().addAll(line);

	            }

	            // labels
	            // TODO: for some reason the text makes the wall have an offset
	            /*
	             for( int y=0; y <= size; y+=size/10) {
	            
	             Text text = new Text( String.valueOf(y));
	             text.setTranslateX(size);
	             text.setTranslateY(y);
	             text.setTranslateZ(zTranslate);
	            
	             getChildren().addAll(text);
	            
	             }*/

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
