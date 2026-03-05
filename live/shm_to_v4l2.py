"""Read frames from VDO.Ninja SHM, push directly to v4l2loopback via GStreamer appsrc."""
import time
import numpy as np
from multiprocessing import shared_memory
import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst

Gst.init(None)

SHM_NAME = "psm_raspininja_streamid"
W, H, FPS = 640, 480, 5

pipeline = Gst.parse_launch(
    f'appsrc name=src format=time is-live=true block=false '
    f'caps=video/x-raw,format=RGB,width={W},height={H},framerate={FPS}/1 '
    f'! videoconvert '
    f'! video/x-raw,format=YUY2,width={W},height={H},framerate={FPS}/1 '
    f'! videorate '
    f'! video/x-raw,format=YUY2,width={W},height={H},framerate=15/1 '
    f'! v4l2sink device=/dev/video50 io-mode=mmap'
)
appsrc = pipeline.get_by_name('src')
pipeline.set_state(Gst.State.PLAYING)

black = bytes(W * H * 3)
frame_ns = Gst.SECOND // FPS
pts = 0
shm = None

print(f"Pushing {FPS}fps to /dev/video50")

while True:
    try:
        if shm is None:
            shm = shared_memory.SharedMemory(name=SHM_NAME, create=False)

        hdr = bytes(shm.buf[:5])
        w = hdr[0] * 255 + hdr[1]
        h = hdr[2] * 255 + hdr[3]

        if w > 0 and h > 0 and w * h * 3 <= shm.size - 5:
            raw = bytes(shm.buf[5:5 + w * h * 3])
            frame = np.frombuffer(raw, dtype=np.uint8).reshape((h, w, 3))
            data = frame[:, :, ::-1].tobytes()  # BGR -> RGB
        else:
            data = black

        buf = Gst.Buffer.new_wrapped(data)
        buf.pts = pts
        buf.duration = frame_ns
        appsrc.emit('push-buffer', buf)
        pts += frame_ns
        time.sleep(1 / FPS)

    except FileNotFoundError:
        shm = None
        buf = Gst.Buffer.new_wrapped(black)
        buf.pts = pts
        buf.duration = frame_ns
        appsrc.emit('push-buffer', buf)
        pts += frame_ns
        time.sleep(1 / FPS)
    except Exception as e:
        print(f"Error: {e}")
        shm = None
        time.sleep(1)
