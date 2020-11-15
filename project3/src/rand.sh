echo "ROS SOA START"
#!/bin/bash

echo "generate random number" 
echo $(( ( RANDOM % 10 )  + 1 ))
(( ( RANDOM % 10 )  + 1 )) &

# gnome-terminal -e "roslaunch project3 myfile.launch" 

echo "script end"
