#!/bin/bash
export DISPLAY=:99 XDG_RUNTIME_DIR=/run/user/0
mkdir -p "$XDG_RUNTIME_DIR" /spike/out && chmod 700 "$XDG_RUNTIME_DIR"
Xvfb :99 -screen 0 1024x640x24 -nolisten tcp >/dev/null 2>&1 & sleep 2
openbox >/dev/null 2>&1 & sleep 1
/usr/libexec/at-spi-bus-launcher --launch-immediately >/dev/null 2>&1 & sleep 2
/usr/libexec/at-spi2-registryd >/dev/null 2>&1 & sleep 2
export QT_QPA_PLATFORM=xcb QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1
build/linux-debug/gui/devmgr-gui > /spike/out/gui.log 2>&1 & sleep 8
python3 /spike/walk.py   > /spike/out/tree.txt   2>&1; echo "walk=$?"   >> /spike/out/tree.txt
python3 /spike/states.py > /spike/out/states.txt 2>&1; echo "states=$?" >> /spike/out/states.txt
import -window root -display :99 /spike/out/devices.png 2>/dev/null
python3 /spike/click.py  > /spike/out/click.txt  2>&1; echo "click=$?"  >> /spike/out/click.txt
import -window root -display :99 /spike/out/modules.png 2>/dev/null
echo "--- captured ---"; ls -1 /spike/out/
