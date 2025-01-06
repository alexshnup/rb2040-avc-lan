export PICO_SDK_PATH=../../../pico-sdk 
export PICO_BOARD=pico
cd build
cmake ..
make
sleep 5
cp avc_lan_capture.uf2 /Volumes/RPI-RP2/
sleep 1
killall NotificationCenter