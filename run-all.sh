#!/bin/sh
/usr/bin/screen -dmS triviabot0 ./run-cluster-0.sh
sleep 60
/usr/bin/screen -dmS triviabot1 ./run-cluster-1.sh
sleep 60
/usr/bin/screen -dmS triviabot2 ./run-cluster-2.sh
sleep 60
/usr/bin/screen -dmS triviabot3 ./run-cluster-3.sh
sleep 60
/usr/bin/screen -dmS triviabot4 ./run-cluster-4.sh
sleep 60
/usr/bin/screen -dmS triviabot5 ./run-cluster-5.sh
sleep 60
/usr/bin/screen -dmS triviabot6 ./run-cluster-6.sh
sleep 60
/usr/bin/screen -dmS triviabot7 ./run-cluster-7.sh
sleep 60

