#!/bin/bash

# UVC POC: Stop everything

echo "=== UVC POC Stop ==="
sudo killall uvc-gadget 2>/dev/null || true
sudo killall gst-launch-1.0 2>/dev/null || true
sleep 1

G=/sys/kernel/config/usb_gadget/poc
if [ -d "$G" ]; then
  echo "" | sudo tee "$G/UDC" >/dev/null 2>&1 || true
  sudo rm "$G/configs/c.1/uvc.0" 2>/dev/null || true
  sudo rmdir "$G/configs/c.1/strings/0x409" "$G/configs/c.1" 2>/dev/null || true
  sudo rmdir "$G/strings/0x409" 2>/dev/null || true
  sudo rmdir "$G/functions/uvc.0"/* 2>/dev/null || true
  sudo rmdir "$G/functions/uvc.0" 2>/dev/null || true
  sudo rmdir "$G" 2>/dev/null || true
fi

sudo rmmod v4l2loopback 2>/dev/null || true
echo "Done."
