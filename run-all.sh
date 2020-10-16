#!/bin/sh
/usr/bin/screen -dmS triviabot0 ./run-cluster-0.sh
sleep 20
/usr/bin/screen -dmS triviabot1 ./run-cluster-1.sh
sleep 20
/usr/bin/screen -dmS triviabot2 ./run-cluster-2.sh
sleep 20
/usr/bin/screen -dmS triviabot3 ./run-cluster-3.sh
sleep 20

