#!/bin/sh
if [ "$1" != "start" ]; then
	echo "stoping"
	killall raspivid
else
	raspivid -t 0 -b 500000 -h 480 -w 640 -fps 20 -g 20 -n -o - | gst-launch-1.0 fdsrc ! h264parse ! rtph264pay config-interval=1 pt=96 ! gdppay ! udpsink port=$3 host=$2 &
	echo "starting;"
fi

