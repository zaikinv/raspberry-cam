# Raspberry Pi Zero 2W USB Webcam

Turn a Pi Zero 2W into a USB webcam visible on Mac (Photo Booth, FaceTime, etc).

Two modes:
1. **Slideshow** — loop local JPEG images
2. **VDO.Ninja** — grab frames from a remote WebRTC stream, then loop them

```
GStreamer (loop JPEGs) → /dev/video50 (v4l2loopback) → uvc-gadget → /dev/video0 (configfs) → USB → Mac
```

---

## Step 0: Flash SD card

Flash [Raspberry Pi OS Lite (64-bit)](https://www.raspberrypi.com/software/) to SD card.

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

## Part 2: Grab frames from VDO.Ninja

Capture 10 frames from a remote browser stream via WebRTC, then loop them as a USB webcam.

### On the Pi:

```bash
cd ~/raspberry_ninja
python3 publish.py --framebuffer STREAMID --password false --noaudio
```

> **IMPORTANT**: Use `--framebuffer STREAMID`, NOT `--view STREAMID --framebuffer WxH`.
> The `--view` flag breaks WebRTC renegotiation on GStreamer 1.22 (pad-added never fires).

### In the browser:

Open `https://vdo.ninja/?push=STREAMID&password=false&width=640&height=480` and allow camera access.

> `&width=640&height=480` constrains the browser stream to match the slideshow resolution.

### Grab frames (in a second terminal on Pi):

```bash
cd ~/raspberry-cam
python3 grab_frames.py STREAMID
```

Saves 10 JPEGs (640x480) to `~/frames/`. Then stop publish.py with Ctrl+C.

Copy frames into the slideshow directory:

```bash
cp ~/frames/frame_*.jpg ~/raspberry-cam/img/
```

### Start slideshow with grabbed frames:

```bash
cd ~/raspberry-cam
./setup.sh && ./run.sh
```

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
| `grab_frames.py` | Captures 10 frames from VDO.Ninja via shared memory, saves to `~/frames/` |
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
