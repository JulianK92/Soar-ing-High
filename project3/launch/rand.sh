#echo "ROS SOA START"
#!/bin/bash

#echo "generate random number" 
#echo $(( ( RANDOM % 12 )  - 6 ))
printf '%s\n' $(echo "scale=2; $RANDOM/32768*(12-0) - 6" | bc )

#echo "script end"
