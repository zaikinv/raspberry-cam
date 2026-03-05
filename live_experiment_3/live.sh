#!/bin/bash
set -euo pipefail

STREAMID="${1:-pitest7x3}"
DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$DIR/.." && pwd)"

pkill -f publish.py 2>/dev/null || true
pkill -f uvc_shm_bridge 2>/dev/null || true
"$PROJECT_ROOT/stop.sh" 2>/dev/null || true
sleep 1

"$DIR/setup.sh"

cd ~/raspberry_ninja
python3 publish.py --framebuffer "$STREAMID" --password false --noaudio >/tmp/live-exp3-publish.log 2>&1 &

# wait for SHM producer
for _ in $(seq 1 20); do
  [ -e /dev/shm/psm_raspininja_streamid ] && break
  sleep 1
done

UVC_DEV=""
for f in /sys/class/video4linux/video*/name; do
  [ -e "$f" ] || continue
  dev="/dev/$(basename "$(dirname "$f")")"
  if grep -Eq "UVC|gadget|3f980000" "$f" 2>/dev/null; then
    UVC_DEV="$dev"
    break
  fi
done
[ -n "$UVC_DEV" ] || UVC_DEV="/dev/video0"

echo "Open: https://vdo.ninja/?push=${STREAMID}&password=false&width=640&height=480"
exec "$DIR/uvc_shm_bridge" "$UVC_DEV"
