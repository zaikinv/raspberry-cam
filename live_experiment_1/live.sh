#!/bin/bash
STREAMID="${1:-pitest7x3}"
DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$DIR/.." && pwd)"

pkill -f publish.py 2>/dev/null || true
pkill -f shm_to_stdout 2>/dev/null || true
"$PROJECT_ROOT/stop.sh" 2>/dev/null || true
sleep 1

"$PROJECT_ROOT/setup.sh"

# VDO.Ninja → shared memory (Experiment 2)
cd ~/raspberry_ninja && python3 publish.py --framebuffer "$STREAMID" --password false --noaudio &

echo "Open: https://vdo.ninja/?push=${STREAMID}&password=false&width=640&height=480"

# SHM → /tmp/live.jpg (atomic single file)
python3 "$PROJECT_ROOT/live/shm_to_stdout.py" &
sleep 2

# Loop single file — no rotation race
gst-launch-1.0 \
  multifilesrc location="/tmp/live.jpg" loop=true caps="image/jpeg,framerate=8/1" ! \
  jpegdec ! videoscale ! videoconvert ! videorate ! \
  "video/x-raw,format=YUY2,width=640,height=480,framerate=15/1" ! \
  v4l2sink device=/dev/video50 io-mode=mmap &

sleep 3

# v4l2loopback → USB (Experiment 1)
"$PROJECT_ROOT/src/uvc-gadget" /dev/video0 /dev/video50
