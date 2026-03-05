# Raspberry Pi Zero 2W USB Webcam

Turn a Pi Zero 2W into a USB webcam visible on Mac (Photo Booth, FaceTime, etc).

## Recommended Live Mode (Current)

Use `live_experiment_3` for the smoothest live VDO.Ninja path:

```
Browser -> publish.py --framebuffer -> POSIX SHM -> uvc_shm_bridge -> /dev/videoX (UVC gadget) -> USB -> Mac
```

One-time on Pi:

```bash
cd ~/raspberry-cam/live_experiment_3
gcc -O2 -Wall -o uvc_shm_bridge uvc_shm_bridge.c
```

Run:

```bash
bash ~/raspberry-cam/live_experiment_3/start.sh pitest7x3
```

Open this sender link in your browser:

`https://vdo.ninja/?push=pitest7x3&password=false&width=640&height=480`

Stop:

```bash
bash ~/raspberry-cam/live_experiment_3/stop.sh
```

Detailed guide: `live_experiment_3/README.md`

Three modes:
1. **Slideshow** — loop local JPEG images
2. **VDO.Ninja live** — stream live WebRTC video to Mac as a USB webcam (~5fps)
3. **VDO.Ninja snapshot** — grab 10 frames from a WebRTC stream and loop them

```
GStreamer (loop JPEGs) → /dev/video50 (v4l2loopback) → uvc-gadget → /dev/video0 (configfs) → USB → Mac
```

---

## Quick Start (fresh OS install)

```bash
# 1. Flash SD, configure WiFi + SSH + dwc2 peripheral (see Step 0 below), boot

# 2. SSH in and clone
ssh pi@raspberrypi.local
git clone https://github.com/zaikinv/raspberry-cam.git ~/raspberry-cam
cd ~/raspberry-cam

# 3. Install everything (takes ~5 min)
./install.sh

# 4. Run a mode (plug USB cable to Mac first)
./setup.sh && ./run.sh                    # Mode 1: slideshow
./grab_and_play.sh STREAMID              # Mode 2: snapshot from VDO.Ninja
bash live/live.sh STREAMID               # Mode 3: live from VDO.Ninja

# Stop
./stop.sh
```

---

## Step 0: Flash SD card

Flash [raspberry_ninja Image]([https://www.raspberrypi.com/software/](https://github.com/steveseguin/raspberry_ninja/blob/main/installers/raspberry_pi/README.md#installing-from-the-provided-image)) to SD card.

**Before ejecting the SD card**, mount the boot partition and:

1. Edit `config.txt` — add before any `[pi4]` or `[all]` section:
   ```
   dtoverlay=dwc2,dr_mode=peripheral
   ```

2. Comment out `otg_mode=1` if present:
   ```
   #otg_mode=1
   ```

3. Enable SSH (create empty file):
   ```bash
   touch /Volumes/bootfs/ssh
   ```

4. Configure WiFi — create `wpa_supplicant.conf` on boot partition:
   ```
   country=US
   ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev
   update_config=1

   network={
       ssid="YOUR_WIFI"
       psk="YOUR_PASSWORD"
   }
   ```

Eject and boot.

## Step 1: Install

```bash
ssh vdo@raspberrypi.local
git clone https://github.com/zaikinv/raspberry-cam.git ~/raspberry-cam
cd ~/raspberry-cam
chmod +x *.sh
./install.sh
```

---

## Part 1: Slideshow mode

Loops 10 random JPEG images as a USB webcam. Each image shows for 1 second.

```bash
cd ~/raspberry-cam
./setup.sh    # configfs gadget + v4l2loopback
./run.sh      # GStreamer slideshow + uvc-gadget bridge
```

Open **Photo Booth** on Mac — looping images appear.

```bash
./stop.sh     # teardown everything
```

## Part 2: Live stream from VDO.Ninja

Stream live video from a WebRTC session directly to Mac as a USB webcam at ~5fps.

### Pipeline

```
Browser → WebRTC → publish.py → POSIX SHM → shm_to_stdout.py → /tmp/live.jpg → GStreamer → /dev/video50 → uvc-gadget → /dev/video0 → USB → Mac
```

### Each node explained

| Node | What it does |
|------|-------------|
| **Browser** | Opens `vdo.ninja/?push=STREAMID` and streams your camera via WebRTC (H264) |
| **publish.py** (`~/raspberry_ninja`) | Receives the WebRTC stream, decodes H264 via GStreamer, and writes raw BGR frames into POSIX shared memory (`psm_raspininja_streamid`). Run with `--framebuffer STREAMID --password false --noaudio`. |
| **POSIX shared memory** | 5-byte header `[w_hi, w_lo, h_hi, h_lo, counter]` followed by `w×h×3` bytes of raw BGR pixel data. Updated at the source frame rate. |
| **shm_to_stdout.py** (`~/live`) | Reads BGR frames from shared memory at 15fps, converts BGR→RGB, saves atomically to `/tmp/live.jpg` (writes to `.tmp.jpg` first, then `os.rename` — so GStreamer never reads a partial JPEG). |
| **GStreamer pipeline** | `multifilesrc` re-reads `/tmp/live.jpg` in a loop at 5fps → `jpegdec` decodes → `videoscale` resizes to 640×480 → `videoconvert` → `videorate` smooths output to 15fps → `v4l2sink` writes to v4l2loopback with `io-mode=mmap`. |
| **/dev/video50** (v4l2loopback) | Virtual V4L2 device. Acts as a shared frame buffer between GStreamer (writer) and uvc-gadget (reader). Loaded with `exclusive_caps=0`. |
| **uvc-gadget** (`~/raspberry-cam/src`) | Bridges v4l2loopback → USB UVC gadget. Reads YUY2 frames from `/dev/video50`, pushes them to `/dev/video0` (the configfs UVC device). Hardcoded to 640×480 YUY2. |
| **/dev/video0** (configfs UVC gadget) | USB peripheral endpoint created via configfs. Presents the Pi to Mac as a standard UVC webcam. |
| **Mac** | Sees a USB webcam. Works in Photo Booth, QuickTime, FaceTime, etc. |

### Run

```bash
bash ~/raspberry-cam/live/live.sh STREAMID
```

Then open in browser:

```
https://vdo.ninja/?push=STREAMID&password=false&width=640&height=480
```

---

## Part 3: Grab frames from VDO.Ninja (snapshot mode)

Capture 10 frames from a remote WebRTC stream and loop them as a USB webcam.

### One command:

```bash
cd ~/raspberry-cam
./grab_and_play.sh STREAMID
```

Then open the printed URL in the browser and allow camera access:

```
https://vdo.ninja/?push=STREAMID&password=false&width=640&height=480
```

The script grabs 10 frames automatically and starts the slideshow.

---

### Manual steps (if needed):

```bash
# 1. Start grab_frames FIRST (it waits for the stream)
python3 grab_frames.py STREAMID &

# 2. Start publish.py
cd ~/raspberry_ninja && python3 publish.py --framebuffer STREAMID --password false --noaudio &

# 3. Open browser URL, then wait for grab to finish
# 4. Copy frames and start slideshow
cp ~/frames/frame_*.jpg ~/raspberry-cam/img/
cd ~/raspberry-cam && ./setup.sh && ./run.sh
```

> **IMPORTANT**: Always start `grab_frames.py` before `publish.py` and the browser — the shared memory only exists while the stream is active and disappears quickly.

> **IMPORTANT**: Use `--framebuffer STREAMID`, NOT `--view STREAMID --framebuffer WxH`.
> The `--view` flag breaks WebRTC renegotiation on GStreamer 1.22 (pad-added never fires).

---

## Powering off

```bash
cd ~/raspberry-cam
./stop.sh
sudo poweroff
```

Wait ~10 seconds for the Pi to shut down, then unplug the USB cable.

> Do NOT unplug without running `stop.sh` first — the configfs gadget holds kernel resources that need a clean teardown.

---

## Hardware

- **Raspberry Pi Zero 2W** (kernel 6.1.21-v8+, aarch64)
- USB data cable to Mac — use the **data port** (closer to center of Pi)
- Single cable carries both data + power

## File overview

| File | Role |
|------|------|
| `install.sh` | One-shot installer: deps + v4l2loopback + compile + sample images |
| `setup.sh` | Creates configfs USB gadget + loads v4l2loopback |
| `run.sh` | GStreamer slideshow → v4l2loopback → uvc-gadget bridge |
| `stop.sh` | Kills processes, tears down configfs gadget |
| `grab_and_play.sh` | One-shot: grab 10 frames from VDO.Ninja + start slideshow |
| `grab_frames.py` | Captures 10 frames from VDO.Ninja via shared memory, saves to `~/frames/` |
| `live/live.sh` | Live mode entry point: starts publish.py + shm_to_stdout.py + GStreamer + uvc-gadget |
| `live/shm_to_stdout.py` | Reads BGR frames from shared memory, writes atomically to `/tmp/live.jpg` at 15fps |
| `src/uvc-gadget.c` | Minimal UVC bridge (hardcoded 640x480 YUY2) |
| `src/uvc.h` | UVC gadget header (userspace only) |
| `src/Makefile` | Compiles uvc-gadget |

## Key facts

| Issue | Resolution |
|-------|------------|
| `dwc_otg` builtin in kernel | Can't use `g_webcam`, use configfs instead |
| v4l2loopback v0.15.3 | DQBUF regression — use **v0.12.5** |
| GStreamer io-mode | Must use `io-mode=mmap` |
| Frame tearing | `videorate` element duplicates frames to 15fps |
| `streaming_maxpacket` | Must be **2048** (not 1024) |
| `exclusive_caps` | Must be **0** |
| configfs symlinks | Must be **relative** (`cd` + `ln -s ../../`) |
| VDO.Ninja `--view` + `--framebuffer` | Broken — use `--framebuffer STREAMID` only |
| `setup.sh` fails with "Device or resource busy" | Run `./stop.sh` first — it removes all nested configfs symlinks in the right order |
