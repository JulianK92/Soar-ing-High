#!/bin/bash

# make bash files executable after download
chmod a+rwx "rand.sh"
chmod a+rwx "rand_Y.sh"

echo "$1"
# starte erstes launchfile mit explore map
gnome-terminal -e "roslaunch project3 myfile.launch my_x:=$(./rand.sh) my_y:=$(./rand.sh) my_Y:=$(./rand_Y.sh)"
sleep 5

# create param for explore node finished or not
rosparam set exploring true
sleep 1

gnome-terminal -e "roslaunch explore_lite explore.launch" 

var=$(rosparam get exploring)
while $var;
do
sleep 10
rosparam get exploring
var=$(rosparam get exploring)
echo "In der Warteschleife!"
done

echo "Endlich an der Reihe"
# Sobald explore node fertig dann speicher map
mapname="mymap$1"
echo "$mapname"
rosrun map_server map_saver -f mymap #$mapname
sleep 5
echo "becoming a serial killer"

killall -9 gzserver
killall -9 gzclient
killall -9 roscore
killall -9 rosmaster
killall rviz

echo "blood everywhere"
sleep 10

echo "one last launch"
# danach launcheeeeee zweites launchfile mit gespeicherter map
gnome-terminal -e "roslaunch project3 launch_mapped_maze.launch my_x:=$(./rand.sh) my_y:=$(./rand.sh) my_Y:=$(./rand_Y.sh)" &

sleep 5

# publishe das goal am exit
rostopic pub /move_base_simple/goal geometry_msgs/PoseStamped '{header: {stamp: now, frame_id: "map"}, pose: {position: {x: 0.5, y: -6.0, z: 0.0}, orientation: {w: 1.0}}}'
