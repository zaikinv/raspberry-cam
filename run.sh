#!/bin/bash
set -euo pipefail

# UVC POC: GStreamer slideshow + uvc-gadget bridge
# 640x480 YUY2

DIR="$(cd "$(dirname "$0")" && pwd)"
IMG_DIR="$DIR/img"
SRC_DIR="$DIR/src"

WIDTH=640
HEIGHT=480

# Verify v4l2loopback
if [ ! -e /dev/video50 ]; then
  echo "ERROR: /dev/video50 not found. Run ./setup.sh first." >&2
  exit 1
fi

# Compile if needed
if [ ! -x "$SRC_DIR/uvc-gadget" ]; then
  echo "Compiling uvc-gadget..."
  make -C "$SRC_DIR"
fi

# Find UVC device
UVC_DEV=""
for f in /sys/class/video4linux/video*/name; do
  [ -e "$f" ] || continue
  dev="/dev/$(basename "$(dirname "$f")")"
  [ "$dev" = "/dev/video50" ] && continue
  grep -q "UVC\|gadget\|3f980000" "$f" 2>/dev/null && UVC_DEV="$dev" && break
done
if [ -z "$UVC_DEV" ]; then
  echo "ERROR: No UVC gadget device found. Run ./setup.sh first." >&2
  exit 1
fi

echo "=== UVC POC ==="
echo "GStreamer → /dev/video50 → uvc-gadget → $UVC_DEV → USB"

# GStreamer slideshow
gst-launch-1.0 \
  multifilesrc location="$IMG_DIR/frame_%d.jpg" index=1 start-index=1 stop-index=10 loop=true caps="image/jpeg,framerate=1/1" \
  ! jpegdec ! videoscale ! videoconvert ! videorate \
  ! "video/x-raw,format=YUY2,width=${WIDTH},height=${HEIGHT},framerate=15/1" \
  ! v4l2sink device=/dev/video50 io-mode=mmap &
GST_PID=$!
sleep 3

# Bridge
"$SRC_DIR/uvc-gadget" "$UVC_DEV" /dev/video50 &
UVC_PID=$!

echo "Running. Photo Booth on Mac should show frames."
echo "Ctrl+C to stop."

cleanup() { kill $UVC_PID $GST_PID 2>/dev/null; wait 2>/dev/null; echo "Stopped."; }
trap cleanup EXIT INT TERM
wait -n $GST_PID $UVC_PID 2>/dev/null || true
