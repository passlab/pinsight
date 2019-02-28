# The color code and name are copied from https://www.rapidtables.com/web/color/Web_Color.html
# The script generates XML elements that we can add to tracecompass XML analysis file to
# display different colors for different processors. The colors and their codes are listed in 
# color_pick.xlsx file and they are picked such that smaller numbered processors have largely 
# distinct colors since they are the processor numbers that are used often. 

num_colors=64

colorcode=("#FF0000" "#FF7F50" "#FFFF00" "#00FF00" "#00FFFF" "#0000FF" "#FF00FF" "#2F4F4F" "#8B0000" "#FFD700" "#BDB76B" "#6B8E23" "#008080" "#5F9EA0" "#00008B" "#800080" "#DB7093" "#8B4513" "#FA8072" "#FF4500" "#FFFFE0" "#FFE4B5" "#32CD32" "#556B2F" "#E0FFFF" "#7B68EE" "#008000" "#FFC0CB" "#ADFF2F" "#808080" "#BC8F8F" "#00CED1" "#1E90FF" "#DDA0DD" "#DEB887" "#4B0082" "#00FA9A" "#DCDCDC" "#A52A2A" "#87CEEB" "#7FFFD4" "#8A2BE2" "#4682B4" "#808000" "#2E8B57" "#00BFFF" "#FF1493" "#D2691E" "#98FB98" "#FF8C00" "#F0E68C" "#9ACD32" "#8FBC8F" "#3CB371" "#FF69B4" "#F4A460" "#AFEEEE" "#E6E6FA" "#9370DB" "#FFE4E1" "#FFF0F5" "#D8BFD8" "#BA55D3" "#C71585")

colornamergb=("red, (255,0,0)" "coral, (255,127,80)" "yellow, (255,255,0)" "lime, (0.255.0)" "aqua, (0,255,255)" "blue, (0,0,255)" "magenta, (255,0,255)" "darkslategray, (47,79,79)" "darkred, (139,0,0)" "gold, (255,215,0)" "darkkhaki, (189,183,107)" "olivedrab, (107,142,35)" "teal, (0,128,128)" "cadetblue, (95,158,160)" "darkblue, (0,0,139)" "purple, (128,0,128)" "palevioletred, (219,112,147)" "saddlebrown, (139,69,19)" "salmon, (250,128,114)" "orangered, (255,69,0)" "lightyellow, (255,255,224)" "moccasin, (255,228,181)" "limegreen, (50,205,50)" "darkolivegreen, (85,107,47)" "lightcyan, (224,255,255)" "mediumslateblue, (123,104,238)" "green, (0,128,0)" "pink, (255,192,203)" "greenyellow, (173,255,47)" "gray, (128,128,128)" "rosybrown, (188,143,143)" "darkturquoise, (0,206,209)" "dodgerblue, (30,144,255)" "plum, (221,160,221)" "burlywood, (222,184,135)" "indigo, (75,0,130)" "mediumspringgreen, (0,250,154)" "gainsboro, (220,220,220)" "brown, (165,42,42)" "skyblue, (135,206,235)" "aquamarine, (127,255,212)" "blueviolet, (138,43,226)" "steelblue, (70,130,180)" "olive, (128,128,0)" "seagreen, (46,139,87)" "deepskyblue, (0,191,255)" "deeppink, (255,20,147)" "chocolate, (210,105,30)" "palegreen, (152,251,152)" "darkorange, (255,140,0)" "khaki, (240,230,140)" "yellowgreen, (154,205,50)" "darkseagreen, (143,188,143)" "mediumseagreen, (60,179,113)" "hotpink, (255,105,180)" "sandybrown, (244,164,96)" "paleturquoise, (175,238,238)" "lavender, (230,230,250)" "mediumpurple, (147,112,219)" "mistyrose, (255,228,225)" "lavenderblush, (255,240,245)" "thistle, (216,191,216)" "mediumorchid, (186,85,211)", "mediumvioletred, rgb(199,21,133)")

for (( i=0; i < ${num_colors}; i++ ));
do 
    color=${colorcode[$i]}
    echo "<definedValue name=\"Processor $i\" value=\"$i\" color=\"${color}\" /> <!-- ${colornamergb[$i]} -->"; 
done
