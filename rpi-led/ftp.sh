#!/bin/bash

HOST='192.168.137.24'
USER='root'
PASSWD=''

ftp -n $HOST <<END_SCRIPT
user $USER 
pass $PASSWD
put rpi-led.ko
quit
END_SCRIPT

exit 0
