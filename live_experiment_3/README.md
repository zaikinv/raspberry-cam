# Live Experiment 3 (Minimal PoC)

## Working Chain

`VDO.Ninja -> publish.py --framebuffer -> uvc_shm_bridge -> /dev/video0 -> USB -> Mac`

This bypasses `v4l2loopback` entirely.

## Files

- `live.sh` : starts setup + `publish.py --framebuffer` + `uvc_shm_bridge`
- `uvc_shm_bridge.c` : minimal bridge from SHM (`/dev/shm/psm_raspininja_streamid`) to UVC gadget `/dev/video0`
- `setup.sh` : reused setup (configfs gadget + kernel modules)
- `uvc.h` : UVC ioctl definitions

## Run

```bash
cd ~/raspberry-cam/live_experiment_3
bash ./live.sh pitest7x3
```

Open sender:

`https://vdo.ninja/?push=pitest7x3&password=false&width=640&height=480`

## Stop / Rollback

```bash
cd ~/raspberry-cam
bash ./stop.sh
```

This returns to clean state; other experiments remain untouched.
