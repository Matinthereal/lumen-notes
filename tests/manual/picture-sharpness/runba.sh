P=${PYTHON:-python3}; S=${PICTURES:?set PICTURES to a folder holding shot1080.png and photo4000.jpg}
$P beforeafter.py ba-shot-fit $S/shot1080.png 300.4 120.6 445 250.3 0,0,0.45,0.5
$P beforeafter.py ba-shot-100 $S/shot1080.png 300.4 120.6 555.8 312.6 0,0,0.4,0.45
$P beforeafter.py ba-shot-300 $S/shot1080.png -33.7 -18.4 1667.4 937.9 0.02,0.02,0.25,0.25
$P beforeafter.py ba-photo-fit $S/photo4000.jpg 300.4 120.6 445 333.75 0.5,0,0.5,0.5
$P beforeafter.py ba-photo-100 $S/photo4000.jpg 300.4 120.6 555.8 416.85 0.5,0,0.5,0.5
$P beforeafter.py ba-photo-300 $S/photo4000.jpg -33.7 -180.4 1667.4 1250.55 0.72,0.2,0.2,0.2
$P beforeafter.py ba-photo-1200 $S/photo4000.jpg -4000.3 -400.6 6669.6 5002.2 0.7,0.15,0.1,0.1
# trimmed to the middle half each way (960x540 of the screenshot), at 100 %; the photo turned a quarter, at 100 %;
# and the photo turned and trimmed (1500x2000 of it), zoomed in past its own pixels
TURN="0 0.25 0.25 0.5 0.5" $P beforeafter.py ba-shot-trimmed-100 $S/shot1080.png 300.4 120.6 277.9 156.3 0,0,1,1
TURN="90 0 0 1 1" $P beforeafter.py ba-photo-turned-100 $S/photo4000.jpg 300.4 120.6 416.85 555.8 0,0.5,0.5,0.5
TURN="90 0.25 0.25 0.5 0.5" $P beforeafter.py ba-photo-turned-trimmed-zoomed $S/photo4000.jpg -1000.3 -1400.6 3000 4000 0.4,0.4,0.15,0.15
