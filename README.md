# Raspberry Pi Zero 2W USB Webcam

Loops JPEG images as a USB webcam visible on Mac (Photo Booth, FaceTime, etc).

```
GStreamer (loop JPEGs) → /dev/video50 (v4l2loopback) → uvc-gadget → /dev/video0 (configfs) → USB → Mac
```

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
   touch /Volumes/boot/ssh
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
chmod +x install.sh setup.sh run.sh stop.sh
./install.sh
```

## Step 2: Run

```bash
cd ~/raspberry-cam
./setup.sh    # configfs gadget + v4l2loopback
./run.sh      # GStreamer slideshow + uvc-gadget bridge
```

Open **Photo Booth** on Mac — looping images appear.

```bash
./stop.sh     # teardown everything
```

## Hardware

- **Raspberry Pi Zero 2W** (kernel 6.1.21-v8+, aarch64)
- USB data cable to Mac — use the **data port** (closer to center of Pi)
- Single cable carries both data + power

## Architecture

| Component | Role |
|-----------|------|
| `setup.sh` | Creates configfs USB gadget + loads v4l2loopback |
| `run.sh` | GStreamer slideshow → v4l2loopback → uvc-gadget bridge |
| `stop.sh` | Kills processes, tears down configfs gadget |
| `src/uvc-gadget.c` | Minimal UVC bridge (270 lines, hardcoded 640x480 YUY2) |
| `install.sh` | One-shot installer: raspberry ninja + deps + v4l2loopback + compile |

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
