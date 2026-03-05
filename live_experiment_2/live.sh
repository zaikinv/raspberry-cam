#!/bin/bash
STREAMID="${1:-pitest7x3}"
DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$DIR/.." && pwd)"

pkill -f publish.py 2>/dev/null || true
"$PROJECT_ROOT/stop.sh" 2>/dev/null || true
sleep 1

SETUP_SH="$PROJECT_ROOT/live_experiment_2/setup.sh"
[ -f "$SETUP_SH" ] || SETUP_SH="$PROJECT_ROOT/setup.sh"
"$SETUP_SH"

PUBLISH_LOG="/tmp/live-exp2-publish.log"
PUBLISH_PY="$PROJECT_ROOT/live_experiment_2/publish.py"
[ -f "$PUBLISH_PY" ] || PUBLISH_PY="$HOME/raspberry_ninja/publish.py"
RN_FORCE_SINK=fakesink python3 -u "$PUBLISH_PY" \
  --view "$STREAMID" \
  --password false \
  --noaudio \
  --buffer 1500 \
  --v4l2sink 50 \
  --v4l2sink-width 640 \
  --v4l2sink-height 480 \
  --v4l2sink-fps 15 \
  --v4l2sink-format YUY2 >"$PUBLISH_LOG" 2>&1 &

echo "Open: https://vdo.ninja/?push=${STREAMID}&password=false&width=640&height=480"

# Use isolated UVC binary if present; fallback to project binary.
UVC_BIN="$PROJECT_ROOT/live_experiment_2/uvc-gadget"
[ -x "$UVC_BIN" ] || UVC_BIN="$PROJECT_ROOT/src/uvc-gadget"

# Avoid UVC STREAMON retries: wait for publish.py to connect first.
for _ in $(seq 1 30); do
  if grep -Eq "Activated source 'remote_src_0'|Activated source 'blank'" "$PUBLISH_LOG" 2>/dev/null; then
    break
  fi
  sleep 1
done

# Ensure loopback has at least one readable frame before starting UVC bridge.
for _ in $(seq 1 20); do
  if v4l2-ctl -d /dev/video50 --stream-mmap=1 --stream-count=1 --stream-to=/dev/null >/dev/null 2>&1; then
    break
  fi
  sleep 1
done

UVC_DEV=""
for f in /sys/class/video4linux/video*/name; do
  [ -e "$f" ] || continue
  dev="/dev/$(basename "$(dirname "$f")")"
  [ "$dev" = "/dev/video50" ] && continue
  if grep -Eq "UVC|gadget|3f980000" "$f" 2>/dev/null; then
    UVC_DEV="$dev"
    break
  fi
done
[ -n "$UVC_DEV" ] || UVC_DEV="/dev/video0"

"$UVC_BIN" "$UVC_DEV" /dev/video50
