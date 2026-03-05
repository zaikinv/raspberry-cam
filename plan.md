# UVC POC Plan

## Goal
3 JPEG frames looping as USB webcam visible on Mac (Photo Booth).

## Pipeline
```
GStreamer (loop JPEGs) → /dev/video50 (v4l2loopback) → uvc-gadget → /dev/video0 (configfs) → USB → Mac
```

## Status: COMPLETE — Photo Booth shows looping frames

## Steps Done

### 1. Created minimal uvc-gadget.c (270 lines)
- Hardcoded 640x480 YUY2
- V4L2 MMAP capture, UVC USERPTR output
- No CLI options, no MJPEG, no bulk, no standalone mode

### 2. Created setup.sh (configfs gadget)
- Originally planned g_webcam, but **dwc_otg is builtin** on this kernel
- g_webcam needs dwc2, dwc2 overlay loads but dwc_otg claims USB first
- Switched to configfs (proven working approach)
- 640x480 YUY2, maxpacket=2048, relative symlinks

### 3. Fixed v4l2loopback
- Latest v0.15.3 has DQBUF regression with GStreamer mmap
- Downgraded to v0.12.5 — works

### 4. Fixed ffmpeg (not used)
- ffmpeg has broken libva-x11 on this Pi (symbol error)
- Not needed — GStreamer works with v0.12.5

### 5. Deployed and verified
- GStreamer + uvc-gadget both running
- Mac Photo Booth shows 5 looping images

### 6. Fixed frame interval
- Changed multifilesrc caps from `framerate=15/1` to `framerate=1/1`
- Each image now displays for 1 second

### 7. Replaced images
- Downloaded 5 random 640x480 images from picsum.photos
- Updated stop-index=3 → stop-index=5

### 8. Fixed frame tearing
- Top/bottom split showing different images
- Added `videorate` element: source at 1fps, output at 15fps
- videorate duplicates frames atomically — no more tearing

## Findings

| Issue | Resolution |
|-------|-----------|
| `otg_mode=1` in config.txt | Commented out |
| `dwc_otg` builtin in kernel | Can't use g_webcam, use configfs instead |
| `dtoverlay=dwc2` after `[pi4]` section | Moved before section filters |
| dwc2 overlay loads but dwc_otg claims device first | Abandoned g_webcam approach |
| v4l2loopback v0.15.3 DQBUF bug | Downgraded to v0.12.5 |
| ffmpeg libva-x11 symbol error | Skipped, using GStreamer |

## Pi Details
- Model: Raspberry Pi Zero 2W Rev 1.0
- Kernel: 6.1.21-v8+ (aarch64)
- User: vdo@raspberrypi.local
- USB driver: dwc_otg (builtin) + dwc2 (module, for UDC)

## Final GStreamer Pipeline
```
multifilesrc (5 JPEGs, 1fps) → jpegdec → videoscale → videoconvert → videorate (15fps) → v4l2sink (mmap)
```

## Next Steps (optional)
- [ ] Test stability (replug USB, reopen Photo Booth)
- [ ] Add systemd service for auto-start on boot
- [ ] Try higher resolution (1280x720)
