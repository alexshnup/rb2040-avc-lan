#!/bin/bash

export PICO_SDK_PATH=../../../pico-sdk 
export PICO_BOARD=pico
cd build
cmake ..
make || exit
# sleep 5


while true; do
    if mount | grep -q '/Volumes/RPI-RP2'; then
        # Пытаемся скопировать файл
        if cp avc_lan_capture.uf2 /Volumes/RPI-RP2/; then
            echo "Файл успешно скопирован."
            break
        else
            echo "Ошибка при копировании файла. Попробую снова..."
        fi
    else
        echo "Диск не подключен. Жду..."
    fi
    sleep 1 # Ждем 5 секунд перед повторной проверкой
done

# cp avc_lan_capture.uf2 /Volumes/RPI-RP2/
sleep 1
killall NotificationCenter
sleep 2
screen /dev/tty.usbmodem11401 115200
reset