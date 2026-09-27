#!/bin/bash

MQTT_SERVER=<PUT SERVERNAME HERE>
MQTT_USER=<PUT USERNAME HERE>
MQTT_PASSWORD=<PUT PASSWORD HERE>

//check can interface is up first
if /sbin/ip addr show can0 | grep -q 'UP'
then
  echo "CAN exists"
else
  echo "Starting CAN"
  /sbin/ip link set can0 up type can bitrate 250000 
fi

IFS=";"
/home/pi/can-ltdc-logger-0.1/can-ltdc-logger can0 | while read  timestamp program name value extra; do
	echo Read: $timestamp $program $name $value $extra
	if [ "$program" == "DLG_RELAY" ]; then
		mosquitto_pub -u $MQTT_USER -P $MQTT_PASSWORD -h $MQTT_SERVER -m "$extra"  -t "sorel/$name/$value" -q 1 -r
	else
		mosquitto_pub -u $MQTT_USER -P $MQTT_PASSWORD -h $MQTT_SERVER -m "$value"  -t "sorel/$name" -q 1 -r
	fi
done
