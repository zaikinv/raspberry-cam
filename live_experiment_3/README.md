# Live Experiment 3

Working minimal live chain for Pi -> USB webcam on Mac.

## Chain

`Browser (VDO.Ninja push) -> publish.py --framebuffer -> /dev/shm/psm_raspininja_streamid -> uvc_shm_bridge -> /dev/videoX (UVC gadget) -> USB -> Mac`

Notes:
- `v4l2loopback` is still loaded by setup for compatibility, but not used in the active video path.
- Current bridge output is fixed at `640x480 YUY2 @30fps`.

## Install (one-time on Pi)

```bash
ssh vdo@raspberrypi.local
cd ~/raspberry-cam/live_experiment_3

# Build bridge binary
gcc -O2 -Wall -o uvc_shm_bridge uvc_shm_bridge.c
chmod +x uvc_shm_bridge start.sh stop.sh live.sh setup.sh
```

Prerequisites expected on Pi:
- `~/raspberry_ninja/publish.py` exists
- kernel has `libcomposite` and `v4l2loopback` available
- passwordless `sudo` for gadget setup commands

## Run

After each reboot or USB replug:

```bash
ssh vdo@raspberrypi.local
bash ~/raspberry-cam/live_experiment_3/start.sh pitest7x3
```

Then open sender URL in browser:

`https://vdo.ninja/?push=pitest7x3&password=false&width=640&height=480`

On Mac, open Photo Booth/QuickTime and select the Pi camera.

## Stop

```bash
ssh vdo@raspberrypi.local
bash ~/raspberry-cam/live_experiment_3/stop.sh
```

## Logs

```bash
tail -f /tmp/pivideo-live3.log
tail -f /tmp/live-exp3-publish.log
```

Expected bridge states:
- `Waiting for host...` = Mac has not started camera stream yet
- `UVC COMMIT ...` = host negotiated stream parameters
- `UVC streaming started` = frames are flowing

## Files

- `start.sh` : one-command start wrapper
- `stop.sh` : one-command stop wrapper
- `live.sh` : setup + launch publish + launch bridge
- `setup.sh` : configfs UVC gadget setup
- `uvc_shm_bridge.c` : SHM(BGR) -> YUY2 UVC bridge
- `uvc.h` : UVC structs/ioctls
