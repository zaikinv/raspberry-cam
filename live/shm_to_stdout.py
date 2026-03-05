"""Read live frames from VDO.Ninja SHM, write JPEGs to /tmp/frames/ in rotation."""
import time, os
import numpy as np
from multiprocessing import shared_memory
from PIL import Image

SHM_NAME = "psm_raspininja_streamid"
DEST = "/tmp/live.jpg"
TMP  = "/tmp/live.tmp.jpg"

# Pre-fill with a black frame so GStreamer can start before stream connects
Image.new('RGB', (640, 480)).save(DEST)

shm = None
while True:
    try:
        if shm is None:
            shm = shared_memory.SharedMemory(name=SHM_NAME, create=False)
        hdr = bytes(shm.buf[:5])
        w = hdr[0] * 255 + hdr[1]
        h = hdr[2] * 255 + hdr[3]
        if w > 0 and h > 0:
            raw = bytes(shm.buf[5:5 + w * h * 3])
            frame = np.frombuffer(raw, dtype=np.uint8).reshape((h, w, 3))
            Image.fromarray(frame[:, :, ::-1]).save(TMP, quality=80)
            os.rename(TMP, DEST)
        time.sleep(1/15)
    except FileNotFoundError:
        shm = None
        time.sleep(1)
