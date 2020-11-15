#echo "ROS SOA START"
#!/bin/bash

#echo "generate random number" 

printf '%s\n' $(echo "scale=2; $RANDOM/32768*(6.28-0) + 0" | bc )

#echo "script end"
