/*
 * Minimal UVC gadget bridge: v4l2loopback → configfs UVC gadget
 * Hardcoded: 640x480 YUY2, UVC device (auto-detect), /dev/video50 (V4L2)
 *
 * Based on wlhe/uvc-gadget (GPL v2)
 */

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/usb/ch9.h>
#include <linux/usb/video.h>
#include <linux/videodev2.h>

#include "uvc.h"

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define clamp(val, lo, hi) ((val) < (lo) ? (lo) : (val) > (hi) ? (hi) : (val))

/* configfs gadget: 640x480 YUY2 */
#define WIDTH  640
#define HEIGHT 480
#define FCC    V4L2_PIX_FMT_YUYV
#define FRAME_SIZE (WIDTH * HEIGHT * 2)

#define NBUFS 4

/* ---------------------------------------------------------------------------
 * Frame table — must match configfs descriptors
 */

struct uvc_frame_info {
    unsigned int width, height;
    unsigned int intervals[8];
};

static const struct uvc_frame_info uvc_frames[] = {
    { 640, 480, { 333333, 333667, 666666, 1000000, 2000000, 0 } },
    { 0, 0, { 0 } },
};

/* ---------------------------------------------------------------------------
 * Buffer
 */

struct buffer {
    struct v4l2_buffer buf;
    void *start;
    size_t length;
};

/* ---------------------------------------------------------------------------
 * V4L2 capture side (v4l2loopback /dev/video50)
 */

struct v4l2_dev {
    int fd;
    int streaming;
    struct buffer *bufs;
    unsigned int nbufs;
    unsigned long long qcnt, dqcnt;
};

static int v4l2_open_dev(struct v4l2_dev *dev, const char *path)
{
    struct v4l2_format fmt;

    dev->fd = open(path, O_RDWR | O_NONBLOCK);
    if (dev->fd < 0) { perror("v4l2 open"); return -1; }

    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = FCC;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    fmt.fmt.pix.sizeimage = FRAME_SIZE;

    if (ioctl(dev->fd, VIDIOC_S_FMT, &fmt) < 0) {
        printf("v4l2: S_FMT failed (%s), using G_FMT if possible\n", strerror(errno));
        if (ioctl(dev->fd, VIDIOC_G_FMT, &fmt) < 0)
            printf("v4l2: G_FMT failed (%s), continuing with default caps\n", strerror(errno));
    }

    return 0;
}

static int v4l2_reqbufs(struct v4l2_dev *dev, int n)
{
    struct v4l2_requestbuffers req = { .count = n, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
    if (ioctl(dev->fd, VIDIOC_REQBUFS, &req) < 0) { perror("v4l2 REQBUFS"); return -1; }
    if (n == 0) {
        dev->nbufs = 0;
        dev->qcnt = 0;
        dev->dqcnt = 0;
        return 0;
    }

    dev->bufs = calloc(req.count, sizeof(struct buffer));
    dev->nbufs = req.count;
    dev->qcnt = 0;
    dev->dqcnt = 0;

    for (unsigned i = 0; i < req.count; i++) {
        CLEAR(dev->bufs[i].buf);
        dev->bufs[i].buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        dev->bufs[i].buf.memory = V4L2_MEMORY_MMAP;
        dev->bufs[i].buf.index = i;
        ioctl(dev->fd, VIDIOC_QUERYBUF, &dev->bufs[i].buf);
        dev->bufs[i].start = mmap(NULL, dev->bufs[i].buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, dev->bufs[i].buf.m.offset);
        dev->bufs[i].length = dev->bufs[i].buf.length;
    }
    return 0;
}

static int v4l2_qbuf_all(struct v4l2_dev *dev)
{
    for (unsigned i = 0; i < dev->nbufs; i++) {
        dev->bufs[i].buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        dev->bufs[i].buf.memory = V4L2_MEMORY_MMAP;
        dev->bufs[i].buf.index = i;
        if (ioctl(dev->fd, VIDIOC_QBUF, &dev->bufs[i].buf) < 0) return -1;
        dev->qcnt++;
    }
    return 0;
}

static int v4l2_streamon(struct v4l2_dev *dev)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (int i = 0; i < 10; i++) {
        if (ioctl(dev->fd, VIDIOC_STREAMON, &type) == 0) return 0;
        printf("v4l2: STREAMON retry %d...\n", i + 1);
        usleep(500000);
    }
    return -1;
}

static void v4l2_cleanup(struct v4l2_dev *dev)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(dev->fd, VIDIOC_STREAMOFF, &type);
    for (unsigned i = 0; i < dev->nbufs; i++)
        munmap(dev->bufs[i].start, dev->bufs[i].length);
    free(dev->bufs);
    dev->bufs = NULL;
    dev->nbufs = 0;
    struct v4l2_requestbuffers req = { .count = 0, .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
    ioctl(dev->fd, VIDIOC_REQBUFS, &req);
    dev->streaming = 0;
    dev->qcnt = 0;
    dev->dqcnt = 0;
}

/* ---------------------------------------------------------------------------
 * UVC output side (g_webcam /dev/video0)
 */

struct uvc_dev {
    int fd;
    int streaming;
    int shutdown;
    unsigned int nbufs;

    struct uvc_streaming_control probe, commit;
    int control;
    unsigned int brightness;

    int first_queued;
    unsigned long long qcnt, dqcnt;

    struct v4l2_dev *vdev;
};

static int uvc_open_dev(struct uvc_dev *dev, const char *path)
{
    dev->fd = open(path, O_RDWR | O_NONBLOCK);
    if (dev->fd < 0) { perror("uvc open"); return -1; }
    return 0;
}

/* ---------------------------------------------------------------------------
 * UVC streaming control negotiation
 */

static void fill_streaming_ctrl(struct uvc_dev *dev, struct uvc_streaming_control *ctrl, int iframe)
{
    const struct uvc_frame_info *f;
    unsigned int nframes = 0;

    while (uvc_frames[nframes].width) nframes++;
    if (iframe < 0) iframe = nframes + iframe;
    iframe = clamp(iframe, 0, (int)nframes - 1);
    f = &uvc_frames[iframe];

    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->bmHint = 1;
    ctrl->bFormatIndex = 1;  /* YUY2 only */
    ctrl->bFrameIndex = iframe + 1;
    ctrl->dwFrameInterval = f->intervals[0];
    ctrl->dwMaxVideoFrameSize = f->width * f->height * 2;
    ctrl->dwMaxPayloadTransferSize = 2048;  /* match configfs streaming_maxpacket */
    ctrl->bmFramingInfo = 3;
    ctrl->bPreferedVersion = 1;
    ctrl->bMaxVersion = 1;
}

/* ---------------------------------------------------------------------------
 * UVC event handling
 */

static void handle_setup(struct uvc_dev *dev, struct usb_ctrlrequest *ctrl, struct uvc_request_data *resp)
{
    uint8_t type = ctrl->bRequestType & USB_TYPE_MASK;
    uint8_t req = ctrl->bRequest;
    uint8_t cs = ctrl->wValue >> 8;
    uint8_t intf = ctrl->wIndex & 0xff;

    dev->control = 0;

    if (type != USB_TYPE_CLASS) return;
    if ((ctrl->bRequestType & USB_RECIP_MASK) != USB_RECIP_INTERFACE) return;

    if (intf == UVC_INTF_CONTROL) {
        /* Minimal: just handle brightness GET_CUR/SET_CUR + error code */
        uint8_t entity = ctrl->wIndex >> 8;
        if (entity == 2 && cs == UVC_PU_BRIGHTNESS_CONTROL) {
            switch (req) {
            case UVC_SET_CUR: resp->data[0] = 0; resp->length = ctrl->wLength; break;
            case UVC_GET_CUR: resp->length = 2; memcpy(resp->data, &dev->brightness, 2); break;
            case UVC_GET_MIN: resp->data[0] = 0; resp->length = 2; break;
            case UVC_GET_MAX: resp->data[0] = 255; resp->length = 2; break;
            case UVC_GET_DEF: resp->data[0] = 127; resp->length = 2; break;
            case UVC_GET_RES: resp->data[0] = 1; resp->length = 2; break;
            case UVC_GET_INFO: resp->data[0] = 0x03; resp->length = 1; break;
            default: resp->length = -EL2HLT; break;
            }
        } else if (entity == 1 && cs == UVC_CT_AE_MODE_CONTROL) {
            switch (req) {
            case UVC_SET_CUR: resp->data[0] = 0x01; resp->length = 1; break;
            case UVC_GET_CUR: case UVC_GET_DEF: case UVC_GET_RES:
                resp->data[0] = 0x02; resp->length = 1; break;
            case UVC_GET_INFO: resp->data[0] = 0x03; resp->length = 1; break;
            default: resp->length = -EL2HLT; break;
            }
        } else if (entity == 0 && cs == UVC_VC_REQUEST_ERROR_CODE_CONTROL) {
            resp->data[0] = 0x06; resp->length = 1;
        }
        return;
    }

    if (intf == UVC_INTF_STREAMING) {
        if (cs != UVC_VS_PROBE_CONTROL && cs != UVC_VS_COMMIT_CONTROL) return;
        struct uvc_streaming_control *sc = (void *)resp->data;
        resp->length = sizeof(*sc);
        switch (req) {
        case UVC_SET_CUR: dev->control = cs; resp->length = 34; break;
        case UVC_GET_CUR:
            memcpy(sc, cs == UVC_VS_PROBE_CONTROL ? &dev->probe : &dev->commit, sizeof(*sc));
            break;
        case UVC_GET_MIN: case UVC_GET_DEF: fill_streaming_ctrl(dev, sc, 0); break;
        case UVC_GET_MAX: fill_streaming_ctrl(dev, sc, -1); break;
        case UVC_GET_RES: memset(sc, 0, sizeof(*sc)); break;
        case UVC_GET_LEN: resp->data[0] = 0x00; resp->data[1] = 0x22; resp->length = 2; break;
        case UVC_GET_INFO: resp->data[0] = 0x03; resp->length = 1; break;
        }
    }
}

static void handle_data(struct uvc_dev *dev, struct uvc_request_data *data)
{
    if (dev->control != UVC_VS_PROBE_CONTROL && dev->control != UVC_VS_COMMIT_CONTROL) {
        /* Brightness SET_CUR */
        memcpy(&dev->brightness, data->data, 2);
        return;
    }

    struct uvc_streaming_control *target = (dev->control == UVC_VS_PROBE_CONTROL) ? &dev->probe : &dev->commit;
    struct uvc_streaming_control *ctrl = (void *)data->data;

    unsigned int nframes = 0;
    while (uvc_frames[nframes].width) nframes++;

    unsigned int iframe = clamp((unsigned)ctrl->bFrameIndex, 1U, nframes);
    const struct uvc_frame_info *f = &uvc_frames[iframe - 1];
    const unsigned int *interval = f->intervals;
    while (interval[0] < ctrl->dwFrameInterval && interval[1]) interval++;

    target->bFormatIndex = 1;
    target->bFrameIndex = iframe;
    target->dwMaxVideoFrameSize = f->width * f->height * 2;
    target->dwFrameInterval = *interval;

    if (dev->control == UVC_VS_COMMIT_CONTROL)
        printf("UVC COMMIT: %ux%u interval=%u\n", f->width, f->height, *interval);
}

static void handle_streamon(struct uvc_dev *dev)
{
    struct v4l2_dev *vdev = dev->vdev;
    dev->qcnt = 0;
    dev->dqcnt = 0;
    dev->first_queued = 0;
    dev->shutdown = 0;

    /* Re-init UVC USERPTR queue each STREAMON for macOS renegotiation. */
    struct v4l2_requestbuffers rb = {
        .count = NBUFS,
        .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
        .memory = V4L2_MEMORY_USERPTR
    };
    if (ioctl(dev->fd, VIDIOC_REQBUFS, &rb) < 0) {
        perror("uvc REQBUFS");
        return;
    }
    dev->nbufs = rb.count;
    if (dev->nbufs == 0) {
        printf("uvc: REQBUFS returned 0 buffers\n");
        return;
    }

    v4l2_reqbufs(vdev, NBUFS);
    v4l2_qbuf_all(vdev);
    if (v4l2_streamon(vdev) < 0) { printf("v4l2 STREAMON failed!\n"); return; }
    vdev->streaming = 1;
    printf("V4L2 streaming started\n");
}

static void handle_streamoff(struct uvc_dev *dev)
{
    if (dev->vdev->streaming)
        v4l2_cleanup(dev->vdev);

    if (dev->streaming) {
        int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        ioctl(dev->fd, VIDIOC_STREAMOFF, &type);
    }

    /* Drop UVC output buffers so next STREAMON starts clean. */
    struct v4l2_requestbuffers rb = {
        .count = 0,
        .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
        .memory = V4L2_MEMORY_USERPTR
    };
    ioctl(dev->fd, VIDIOC_REQBUFS, &rb);
    dev->nbufs = 0;

    /* Always reset output state on STREAMOFF, even if already stopped. */
    dev->streaming = 0;
    dev->first_queued = 0;
    dev->qcnt = 0;
    dev->dqcnt = 0;
    dev->shutdown = 0;
    printf("UVC streaming stopped\n");
}

static void process_uvc_event(struct uvc_dev *dev)
{
    struct v4l2_event ev;
    struct uvc_event *uev = (void *)&ev.u.data;
    struct uvc_request_data resp;

    if (ioctl(dev->fd, VIDIOC_DQEVENT, &ev) < 0) return;

    memset(&resp, 0, sizeof(resp));
    resp.length = -EL2HLT;

    switch (ev.type) {
    case UVC_EVENT_CONNECT: return;
    case UVC_EVENT_DISCONNECT: dev->shutdown = 1; return;
    case UVC_EVENT_SETUP: handle_setup(dev, &uev->req, &resp); break;
    case UVC_EVENT_DATA: handle_data(dev, &uev->data); return;
    case UVC_EVENT_STREAMON: handle_streamon(dev); return;
    case UVC_EVENT_STREAMOFF: handle_streamoff(dev); return;
    }

    ioctl(dev->fd, UVCIOC_SEND_RESPONSE, &resp);
}

/* ---------------------------------------------------------------------------
 * Frame bridging: V4L2 capture → UVC output
 */

static void process_v4l2_frame(struct v4l2_dev *vdev, struct uvc_dev *udev)
{
    struct v4l2_buffer vbuf, ubuf;

    if (!vdev->streaming) return;
    if (udev->first_queued && vdev->dqcnt >= vdev->qcnt) return;

    CLEAR(vbuf);
    vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vbuf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(vdev->fd, VIDIOC_DQBUF, &vbuf) < 0) return;
    vdev->dqcnt++;

    /* Keep V4L2 queue healthy across host renegotiation/error frames. */
    if (vbuf.flags & V4L2_BUF_FLAG_ERROR) {
        if (ioctl(vdev->fd, VIDIOC_QBUF, &vbuf) == 0)
            vdev->qcnt++;
        return;
    }

    CLEAR(ubuf);
    ubuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ubuf.memory = V4L2_MEMORY_USERPTR;
    ubuf.m.userptr = (unsigned long)vdev->bufs[vbuf.index].start;
    ubuf.length = vdev->bufs[vbuf.index].length;
    ubuf.index = udev->nbufs ? (vbuf.index % udev->nbufs) : 0;
    ubuf.bytesused = vbuf.bytesused;

    if (ioctl(udev->fd, VIDIOC_QBUF, &ubuf) < 0) {
        if (!udev->first_queued || errno == EINVAL || errno == EBUSY) {
            printf("uvc QBUF failed: %s (idx=%u bytes=%u len=%u nbufs=%u)\n",
                   strerror(errno), ubuf.index, ubuf.bytesused, ubuf.length, udev->nbufs);
        }
        if (errno == ENODEV) udev->shutdown = 1;
        if (ioctl(vdev->fd, VIDIOC_QBUF, &vbuf) == 0)
            vdev->qcnt++;
        return;
    }
    udev->qcnt++;

    if (!udev->first_queued) {
        int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        ioctl(udev->fd, VIDIOC_STREAMON, &type);
        udev->first_queued = 1;
        udev->streaming = 1;
        printf("UVC streaming started\n");
    }
}

static void process_uvc_output(struct uvc_dev *udev)
{
    struct v4l2_buffer ubuf, vbuf;

    if (!udev->streaming) return;
    if (!udev->vdev->streaming || !udev->first_queued) return;
    if (!udev->shutdown && (udev->dqcnt + 1) >= udev->qcnt) return;

    CLEAR(ubuf);
    ubuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ubuf.memory = V4L2_MEMORY_USERPTR;
    if (ioctl(udev->fd, VIDIOC_DQBUF, &ubuf) < 0) return;
    udev->dqcnt++;

    /* Re-queue to V4L2 capture */
    unsigned int idx = ubuf.index;
    for (unsigned i = 0; i < udev->vdev->nbufs; i++)
        if (ubuf.m.userptr == (unsigned long)udev->vdev->bufs[i].start) { idx = i; break; }

    CLEAR(vbuf);
    vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vbuf.memory = V4L2_MEMORY_MMAP;
    vbuf.index = idx;
    if (ioctl(udev->vdev->fd, VIDIOC_QBUF, &vbuf) == 0)
        udev->vdev->qcnt++;
}

/* ---------------------------------------------------------------------------
 * Main
 */

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    const char *uvc_path = (argc > 1) ? argv[1] : "/dev/video0";
    const char *v4l2_path = (argc > 2) ? argv[2] : "/dev/video50";

    struct v4l2_dev vdev = {0};
    struct uvc_dev udev = {0};
    udev.vdev = &vdev;

    printf("UVC POC: %s (v4l2) -> %s (uvc) | %dx%d YUY2\n", v4l2_path, uvc_path, WIDTH, HEIGHT);

    if (v4l2_open_dev(&vdev, v4l2_path) < 0) return 1;
    if (uvc_open_dev(&udev, uvc_path) < 0) return 1;

    /* Allocate V4L2 USERPTR buffers on UVC side */
    struct v4l2_requestbuffers rb = { .count = NBUFS, .type = V4L2_BUF_TYPE_VIDEO_OUTPUT, .memory = V4L2_MEMORY_USERPTR };
    if (ioctl(udev.fd, VIDIOC_REQBUFS, &rb) == 0)
        udev.nbufs = rb.count;

    /* Init UVC events */
    fill_streaming_ctrl(&udev, &udev.probe, 0);
    fill_streaming_ctrl(&udev, &udev.commit, 0);

    struct v4l2_event_subscription sub = {0};
    int events[] = { UVC_EVENT_SETUP, UVC_EVENT_DATA, UVC_EVENT_STREAMON, UVC_EVENT_STREAMOFF, 0 };
    for (int i = 0; events[i]; i++) { sub.type = events[i]; ioctl(udev.fd, VIDIOC_SUBSCRIBE_EVENT, &sub); }

    printf("Waiting for host...\n");

    /* Main loop */
    while (1) {
        fd_set rfds, wfds, efds;
        struct timeval tv = { .tv_sec = 2 };

        FD_ZERO(&rfds); FD_ZERO(&wfds); FD_ZERO(&efds);
        FD_SET(udev.fd, &efds);
        if (udev.streaming) FD_SET(udev.fd, &wfds);
        if (vdev.streaming) FD_SET(vdev.fd, &rfds);

        int nfds = max(udev.fd, vdev.fd);
        int ret = select(nfds + 1, &rfds, &wfds, &efds, &tv);
        if (ret < 0) { if (errno == EINTR) continue; break; }
        if (ret == 0) continue;

        if (FD_ISSET(udev.fd, &efds)) process_uvc_event(&udev);
        if (FD_ISSET(udev.fd, &wfds)) process_uvc_output(&udev);
        if (FD_ISSET(vdev.fd, &rfds)) process_v4l2_frame(&vdev, &udev);
    }

    handle_streamoff(&udev);
    close(vdev.fd);
    close(udev.fd);
    return 0;
}
