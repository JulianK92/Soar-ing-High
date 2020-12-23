#!/bin/bash

counter=0

while true;
    do
    bash simple_goal.sh $counter
    counter=$((counter+1))
    echo $counter
    if [ "$counter" -eq "10" ]; then
        exit
    fi
done