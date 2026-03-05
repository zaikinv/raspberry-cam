# Live Experiment 3

Minimal direct path PoC (no v4l2loopback in active video path).

## Chain

`VDO.Ninja -> publish.py --framebuffer -> /dev/shm/psm_raspininja_streamid -> uvc_shm_bridge -> /dev/videoX (UVC gadget) -> USB -> Mac`

## One-command start (after every replug/reboot)

```bash
ssh vdo@raspberrypi.local
bash ~/raspberry-cam/live_experiment_3/start.sh pitest7x3
```

Open sender URL:

`https://vdo.ninja/?push=pitest7x3&password=false&width=640&height=480`

## One-command stop

```bash
ssh vdo@raspberrypi.local
bash ~/raspberry-cam/live_experiment_3/stop.sh
```

## Logs

Bridge log:

```bash
tail -f /tmp/pivideo-live3.log
```

Publish log:

```bash
tail -f /tmp/live-exp3-publish.log
```

## Expected bridge states

- `Waiting for host...` : Mac app has not opened camera yet.
- `UVC COMMIT ...` : Mac negotiated stream params.
- `UVC streaming started` : frames are being pushed to USB camera path.

## Files

- `live.sh` : setup + start publish + start bridge
- `start.sh` : shortest user command wrapper
- `stop.sh` : clean stop wrapper
- `uvc_shm_bridge.c` : SHM -> UVC bridge
- `setup.sh` : configfs gadget setup reuse
- `uvc.h` : UVC ioctl definitions
