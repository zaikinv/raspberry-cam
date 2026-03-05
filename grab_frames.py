"""
Grab 10 frames from a VDO.Ninja stream via shared memory.

Prerequisites:
  1. Run publish.py in framebuffer mode:
     cd ~/raspberry_ninja
     python3 publish.py --framebuffer STREAMID --password false --noaudio

  2. Open in browser:
     https://vdo.ninja/?push=STREAMID&password=false

  3. Run this script:
     python3 grab_frames.py STREAMID

IMPORTANT: Do NOT use --view with --framebuffer. It breaks WebRTC
renegotiation on GStreamer 1.22 (pad-added never fires).
"""
import sys, os, time, random
import numpy as np
from multiprocessing import shared_memory
from PIL import Image

STREAMID = sys.argv[1] if len(sys.argv) > 1 else "pitest123"
SHM_NAME = "psm_raspininja_streamid"
FRAMES_DIR = os.path.expanduser("~/frames")
os.makedirs(FRAMES_DIR, exist_ok=True)

print(f"Waiting for shared memory: {SHM_NAME}")
print(f"Will save 10 frames to {FRAMES_DIR}/")
print(f"Make sure publish.py --framebuffer {STREAMID} is running.")

shm = None
for i in range(120):
    try:
        shm = shared_memory.SharedMemory(name=SHM_NAME, create=False)
        break
    except FileNotFoundError:
        time.sleep(1)
        if i % 10 == 0:
            print(f"  waiting... ({i}s)")

if not shm:
    print("Timeout. Is publish.py --framebuffer running?")
    sys.exit(1)

print(f"Connected to shared memory ({shm.size} bytes). Grabbing frames...")
time.sleep(3)

saved = 0
last_counter = -1
for attempt in range(90):
    hdr = bytes(shm.buf[:5])
    w = hdr[0] * 255 + hdr[1]
    h = hdr[2] * 255 + hdr[3]
    counter = hdr[4]

    if w > 0 and h > 0 and counter != last_counter:
        nbytes = w * h * 3
        raw = bytes(shm.buf[5:5 + nbytes])
        frame = np.frombuffer(raw, dtype=np.uint8).reshape((h, w, 3))
        frame_rgb = frame[:, :, ::-1]  # BGR -> RGB
        img = Image.fromarray(frame_rgb)
        path = os.path.join(FRAMES_DIR, f"frame_{saved + 1}.jpg")
        img.save(path, quality=90)
        print(f"  Saved {path} ({w}x{h}, counter={counter})")
        last_counter = counter
        saved += 1
        if saved >= 10:
            break
        time.sleep(random.uniform(2, 4))
    else:
        time.sleep(0.3)

shm.close()
if saved >= 10:
    print(f"Done! {saved} frames saved to {FRAMES_DIR}/")
else:
    print(f"Only got {saved} frames after {attempt + 1} attempts.")
