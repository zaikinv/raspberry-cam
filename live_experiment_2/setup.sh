#!/bin/bash
set -euo pipefail

# UVC POC: configfs gadget + v4l2loopback
# Creates /dev/videoX (UVC) and /dev/video50 (loopback)

G=/sys/kernel/config/usb_gadget/poc

echo "=== UVC POC Setup ==="

# v4l2loopback
if ! lsmod | grep -q '^v4l2loopback'; then
  sudo modprobe v4l2loopback video_nr=50 exclusive_caps=0 max_buffers=8 || \
    sudo insmod ~/v4l2loopback/v4l2loopback.ko video_nr=50 exclusive_caps=0 max_buffers=8
  echo "Loaded v4l2loopback → /dev/video50"
fi

# configfs gadget
sudo modprobe libcomposite

# Tear down if exists
if [ -f "$G/UDC" ] && [ -n "$(cat "$G/UDC" 2>/dev/null)" ]; then
  echo "" | sudo tee "$G/UDC" >/dev/null
fi
if [ -L "$G/configs/c.1/uvc.0" ]; then
  sudo rm "$G/configs/c.1/uvc.0"
fi

sudo mkdir -p "$G"
echo 0x1d6b | sudo tee "$G/idVendor" >/dev/null
echo 0x0104 | sudo tee "$G/idProduct" >/dev/null
echo 0x0100 | sudo tee "$G/bcdDevice" >/dev/null
echo 0x0200 | sudo tee "$G/bcdUSB" >/dev/null

sudo mkdir -p "$G/strings/0x409"
echo "0123456789"   | sudo tee "$G/strings/0x409/serialnumber" >/dev/null
echo "UVC POC"      | sudo tee "$G/strings/0x409/manufacturer" >/dev/null
echo "UVC POC Cam"  | sudo tee "$G/strings/0x409/product" >/dev/null

sudo mkdir -p "$G/configs/c.1/strings/0x409"
echo "UVC" | sudo tee "$G/configs/c.1/strings/0x409/configuration" >/dev/null
echo 250   | sudo tee "$G/configs/c.1/MaxPower" >/dev/null

sudo mkdir -p "$G/functions/uvc.0"

# 640x480 YUY2
FRAME="$G/functions/uvc.0/streaming/uncompressed/u/480p"
sudo mkdir -p "$FRAME"
echo 640              | sudo tee "$FRAME/wWidth" >/dev/null
echo 480              | sudo tee "$FRAME/wHeight" >/dev/null
echo $((640*480*2))   | sudo tee "$FRAME/dwMaxVideoFrameBufferSize" >/dev/null
# Include 29.97fps (333667) for AVFoundation compatibility.
printf '333667\n333333\n666666\n1000000\n2000000\n' | sudo tee "$FRAME/dwFrameInterval" >/dev/null

# Streaming header + symlinks
sudo mkdir -p "$G/functions/uvc.0/streaming/header/h"
(
  cd "$G/functions/uvc.0/streaming/header/h"
  [ -L u ] || sudo ln -s ../../uncompressed/u u
  cd ../../class/fs;  [ -L h ] || sudo ln -s ../../header/h h
  cd ../hs;           [ -L h ] || sudo ln -s ../../header/h h
  cd ../ss;           [ -L h ] || sudo ln -s ../../header/h h
)

# Control header
(
  cd "$G/functions/uvc.0/control"
  sudo mkdir -p header/h class/fs class/ss
  cd class/fs; [ -L h ] || sudo ln -s ../../header/h h
  cd ../ss;    [ -L h ] || sudo ln -s ../../header/h h
)

echo 2048 | sudo tee "$G/functions/uvc.0/streaming_maxpacket" >/dev/null 2>&1 || true

# Bind
(cd "$G" && [ -L configs/c.1/uvc.0 ] || sudo ln -s "$G/functions/uvc.0" "$G/configs/c.1/")
UDC="$(ls /sys/class/udc | head -n1)"
echo "$UDC" | sudo tee "$G/UDC" >/dev/null

echo "Gadget bound to $UDC"

# Find UVC video device
sleep 1
UVC_DEV=""
for f in /sys/class/video4linux/video*/name; do
  [ -e "$f" ] || continue
  dev="/dev/$(basename "$(dirname "$f")")"
  [ "$dev" = "/dev/video50" ] && continue
  grep -q "UVC\|gadget\|3f980000" "$f" 2>/dev/null && UVC_DEV="$dev" && break
done

echo ""
echo "UVC device: ${UVC_DEV:-not found yet}"
echo "V4L2 loopback: /dev/video50"
echo ""
echo "Run: ./run.sh"
