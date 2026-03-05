#!/bin/bash

# UVC POC: Stop everything

echo "=== UVC POC Stop ==="
sudo killall uvc-gadget 2>/dev/null || true
sudo killall gst-launch-1.0 2>/dev/null || true
sleep 1

G=/sys/kernel/config/usb_gadget/poc
if [ -d "$G" ]; then
  echo "" | sudo tee "$G/UDC" >/dev/null 2>&1 || true
  sudo rm -f "$G/configs/c.1/uvc.0"
  # Remove symlinks first (rm), then dirs from deepest to shallowest (rmdir)
  sudo rm -f "$G/functions/uvc.0/streaming/class/fs/h"
  sudo rm -f "$G/functions/uvc.0/streaming/class/hs/h"
  sudo rm -f "$G/functions/uvc.0/streaming/class/ss/h"
  sudo rm -f "$G/functions/uvc.0/streaming/header/h/u"
  sudo rm -f "$G/functions/uvc.0/control/class/fs/h"
  sudo rm -f "$G/functions/uvc.0/control/class/ss/h"
  sudo rmdir "$G/functions/uvc.0/streaming/class/fs" "$G/functions/uvc.0/streaming/class/hs" "$G/functions/uvc.0/streaming/class/ss" "$G/functions/uvc.0/streaming/class" 2>/dev/null || true
  sudo rmdir "$G/functions/uvc.0/streaming/header/h" "$G/functions/uvc.0/streaming/header" 2>/dev/null || true
  sudo rmdir "$G/functions/uvc.0/streaming/uncompressed/u/480p" "$G/functions/uvc.0/streaming/uncompressed/u" "$G/functions/uvc.0/streaming/uncompressed" "$G/functions/uvc.0/streaming" 2>/dev/null || true
  sudo rmdir "$G/functions/uvc.0/control/class/fs" "$G/functions/uvc.0/control/class/ss" "$G/functions/uvc.0/control/class" 2>/dev/null || true
  sudo rmdir "$G/functions/uvc.0/control/header/h" "$G/functions/uvc.0/control/header" "$G/functions/uvc.0/control" 2>/dev/null || true
  sudo rmdir "$G/functions/uvc.0" 2>/dev/null || true
  sudo rmdir "$G/configs/c.1/strings/0x409" "$G/configs/c.1" "$G/strings/0x409" "$G" 2>/dev/null || true
fi

sudo rmmod v4l2loopback 2>/dev/null || true
echo "Done."
