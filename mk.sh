#!/bin/bash

export PICO_SDK_PATH=../../../pico-sdk 
export PICO_BOARD=pico
cd build
cmake ..
make || exit
# sleep 5


while true; do
    if mount | grep -q '/Volumes/RPI-RP2'; then
        # Attempt to copy the file
        if cp avc_lan_capture.uf2 /Volumes/RPI-RP2/; then
            echo "Copy ok."
            break
        else
            echo "Error copy... Will try again"
        fi
    else
        echo "Volume is not connected, Waiting..."
    fi
    sleep 1 # Wait 1 second before checking again
done

# cp avc_lan_capture.uf2 /Volumes/RPI-RP2/
sleep 1
killall NotificationCenter
# sleep 2
# screen /dev/tty.usbmodem11401 115200
# reset
