#!/bin/bash
STREAMID="${1:-pitest7x3}"
DIR="$(cd "$(dirname "$0")" && pwd)"

# Stop slideshow and any stale publish.py
"$DIR/stop.sh" 2>/dev/null || true
pkill -f publish.py 2>/dev/null || true
sleep 1

# Start grab_frames first, then publish.py
python3 "$DIR/grab_frames.py" "$STREAMID" &
GRAB_PID=$!
sleep 2
cd ~/raspberry_ninja && python3 publish.py --framebuffer "$STREAMID" --password false --noaudio &
PUBLISH_PID=$!

echo "Open: https://vdo.ninja/?push=${STREAMID}&password=false&width=640&height=480"
wait $GRAB_PID

kill $PUBLISH_PID 2>/dev/null || true
cp ~/frames/frame_*.jpg "$DIR/img/"
"$DIR/setup.sh" && "$DIR/run.sh"
