#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/usb/ch9.h>
#include <linux/usb/video.h>
#include <linux/videodev2.h>

#include "uvc.h"

#define CLEAR(x) memset(&(x), 0, sizeof(x))
#define clamp(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

#define WIDTH 640
#define HEIGHT 480
#define FRAME_SIZE (WIDTH * HEIGHT * 2)
#define NBUFS 4
#define TARGET_FPS 20
#define FRAME_NS (1000000000ULL / TARGET_FPS)

#define SHM_PATH "/dev/shm/psm_raspininja_streamid"

struct uvc_frame_info {
    unsigned int width, height;
    unsigned int intervals[8];
};

static const struct uvc_frame_info uvc_frames[] = {
    { 640, 480, { 333333, 333667, 500000, 666666, 1000000, 2000000, 0 } },
    { 0, 0, { 0 } },
};

struct outbuf {
    void *ptr;
    size_t len;
    int queued;
};

struct shm_state {
    int fd;
    uint8_t *ptr;
    size_t size;
    uint8_t last_counter;
    int have_frame;
    uint8_t *latest_yuyv;
};

struct uvc_dev {
    int fd;
    int streaming;
    int shutdown;

    struct uvc_streaming_control probe, commit;
    int control;
    unsigned int brightness;

    int first_queued;
    unsigned int nbufs;
    unsigned long long qcnt, dqcnt;

    struct outbuf bufs[NBUFS];
    struct shm_state shm;
    uint64_t next_push_ns;
};

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int uvc_open_dev(struct uvc_dev *dev, const char *path)
{
    dev->fd = open(path, O_RDWR | O_NONBLOCK);
    if (dev->fd < 0) {
        perror("uvc open");
        return -1;
    }
    return 0;
}

static void shm_close_state(struct shm_state *s)
{
    if (s->ptr && s->size)
        munmap(s->ptr, s->size);
    if (s->fd >= 0)
        close(s->fd);
    s->fd = -1;
    s->ptr = NULL;
    s->size = 0;
}

static int shm_open_state(struct shm_state *s)
{
    struct stat st;
    s->fd = open(SHM_PATH, O_RDONLY);
    if (s->fd < 0)
        return -1;
    if (fstat(s->fd, &st) < 0 || st.st_size < 6) {
        close(s->fd);
        s->fd = -1;
        return -1;
    }
    s->size = (size_t)st.st_size;
    s->ptr = mmap(NULL, s->size, PROT_READ, MAP_SHARED, s->fd, 0);
    if (s->ptr == MAP_FAILED) {
        s->ptr = NULL;
        close(s->fd);
        s->fd = -1;
        return -1;
    }
    return 0;
}

static inline uint8_t clamp_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* BGR24 -> YUYV422 (no scaling, source must be WIDTHxHEIGHT). */
static void bgr_to_yuyv_native(const uint8_t *src, uint8_t *dst)
{
    for (int y = 0; y < HEIGHT; y++) {
        const uint8_t *srow = src + (size_t)y * WIDTH * 3;
        uint8_t *drow = dst + (size_t)y * WIDTH * 2;

        for (int x = 0; x < WIDTH; x += 2) {
            const uint8_t *p0 = srow + (size_t)x * 3;
            const uint8_t *p1 = p0 + 3;

            int b0 = p0[0], g0 = p0[1], r0 = p0[2];
            int b1 = p1[0], g1 = p1[1], r1 = p1[2];

            int y0 = (( 66 * r0 + 129 * g0 +  25 * b0 + 128) >> 8) + 16;
            int y1 = (( 66 * r1 + 129 * g1 +  25 * b1 + 128) >> 8) + 16;
            int u0 = ((-38 * r0 -  74 * g0 + 112 * b0 + 128) >> 8) + 128;
            int v0 = ((112 * r0 -  94 * g0 -  18 * b0 + 128) >> 8) + 128;
            int u1 = ((-38 * r1 -  74 * g1 + 112 * b1 + 128) >> 8) + 128;
            int v1 = ((112 * r1 -  94 * g1 -  18 * b1 + 128) >> 8) + 128;

            int u = (u0 + u1) / 2;
            int v = (v0 + v1) / 2;

            drow[x * 2 + 0] = clamp_u8(y0);
            drow[x * 2 + 1] = clamp_u8(u);
            drow[x * 2 + 2] = clamp_u8(y1);
            drow[x * 2 + 3] = clamp_u8(v);
        }
    }
}

/* Nearest-neighbor scale + BGR24 -> YUYV422 */
static void bgr_to_yuyv_scaled(const uint8_t *src, int sw, int sh, uint8_t *dst)
{
    for (int y = 0; y < HEIGHT; y++) {
        int sy = (int)((long long)y * sh / HEIGHT);
        const uint8_t *srow = src + (size_t)sy * sw * 3;
        uint8_t *drow = dst + (size_t)y * WIDTH * 2;

        for (int x = 0; x < WIDTH; x += 2) {
            int sx0 = (int)((long long)x * sw / WIDTH);
            int sx1 = (int)((long long)(x + 1) * sw / WIDTH);

            const uint8_t *p0 = srow + (size_t)sx0 * 3;
            const uint8_t *p1 = srow + (size_t)sx1 * 3;

            int b0 = p0[0], g0 = p0[1], r0 = p0[2];
            int b1 = p1[0], g1 = p1[1], r1 = p1[2];

            int y0 = (( 66 * r0 + 129 * g0 +  25 * b0 + 128) >> 8) + 16;
            int y1 = (( 66 * r1 + 129 * g1 +  25 * b1 + 128) >> 8) + 16;
            int u0 = ((-38 * r0 -  74 * g0 + 112 * b0 + 128) >> 8) + 128;
            int v0 = ((112 * r0 -  94 * g0 -  18 * b0 + 128) >> 8) + 128;
            int u1 = ((-38 * r1 -  74 * g1 + 112 * b1 + 128) >> 8) + 128;
            int v1 = ((112 * r1 -  94 * g1 -  18 * b1 + 128) >> 8) + 128;

            int u = (u0 + u1) / 2;
            int v = (v0 + v1) / 2;

            drow[x * 2 + 0] = clamp_u8(y0);
            drow[x * 2 + 1] = clamp_u8(u);
            drow[x * 2 + 2] = clamp_u8(y1);
            drow[x * 2 + 3] = clamp_u8(v);
        }
    }
}

static int shm_update_latest(struct shm_state *s)
{
    if (!s->ptr) {
        if (shm_open_state(s) < 0)
            return 0;
    }

    if (s->size < 6)
        return 0;

    uint8_t *hdr = s->ptr;
    int w = hdr[0] * 255 + hdr[1];
    int h = hdr[2] * 255 + hdr[3];
    uint8_t counter = hdr[4];

    if (w <= 0 || h <= 0)
        return 0;
    size_t need = 5 + (size_t)w * (size_t)h * 3;
    if (need > s->size)
        return 0;

    if (counter == s->last_counter && s->have_frame)
        return 0;

    if (w == WIDTH && h == HEIGHT)
        bgr_to_yuyv_native(s->ptr + 5, s->latest_yuyv);
    else
        bgr_to_yuyv_scaled(s->ptr + 5, w, h, s->latest_yuyv);
    s->last_counter = counter;
    s->have_frame = 1;
    return 1;
}

static void fill_streaming_ctrl(struct uvc_streaming_control *ctrl, int iframe)
{
    const struct uvc_frame_info *f;
    unsigned int nframes = 0;

    while (uvc_frames[nframes].width) nframes++;
    if (iframe < 0) iframe = (int)nframes + iframe;
    iframe = clamp(iframe, 0, (int)nframes - 1);
    f = &uvc_frames[iframe];

    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->bmHint = 1;
    ctrl->bFormatIndex = 1;
    ctrl->bFrameIndex = iframe + 1;
    ctrl->dwFrameInterval = f->intervals[0];
    ctrl->dwMaxVideoFrameSize = f->width * f->height * 2;
    ctrl->dwMaxPayloadTransferSize = 2048;
    ctrl->bmFramingInfo = 3;
    ctrl->bPreferedVersion = 1;
    ctrl->bMaxVersion = 1;
}

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
        } else if (entity == 0 && cs == UVC_VC_REQUEST_ERROR_CODE_CONTROL) {
            resp->data[0] = 0x06;
            resp->length = 1;
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
        case UVC_GET_MIN: case UVC_GET_DEF: fill_streaming_ctrl(sc, 0); break;
        case UVC_GET_MAX: fill_streaming_ctrl(sc, -1); break;
        case UVC_GET_RES: memset(sc, 0, sizeof(*sc)); break;
        case UVC_GET_LEN: resp->data[0] = 0x00; resp->data[1] = 0x22; resp->length = 2; break;
        case UVC_GET_INFO: resp->data[0] = 0x03; resp->length = 1; break;
        }
    }
}

static void handle_data(struct uvc_dev *dev, struct uvc_request_data *data)
{
    if (dev->control != UVC_VS_PROBE_CONTROL && dev->control != UVC_VS_COMMIT_CONTROL) {
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

static void free_userptr_bufs(struct uvc_dev *dev)
{
    for (unsigned i = 0; i < dev->nbufs; i++) {
        free(dev->bufs[i].ptr);
        dev->bufs[i].ptr = NULL;
        dev->bufs[i].len = 0;
        dev->bufs[i].queued = 0;
    }
    dev->nbufs = 0;
}

static int alloc_userptr_bufs(struct uvc_dev *dev)
{
    struct v4l2_requestbuffers rb;
    CLEAR(rb);
    rb.count = NBUFS;
    rb.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    rb.memory = V4L2_MEMORY_USERPTR;

    if (ioctl(dev->fd, VIDIOC_REQBUFS, &rb) < 0) {
        perror("uvc REQBUFS");
        return -1;
    }

    dev->nbufs = rb.count;
    if (dev->nbufs == 0) {
        printf("uvc REQBUFS returned 0\n");
        return -1;
    }
    if (dev->nbufs > NBUFS)
        dev->nbufs = NBUFS;

    for (unsigned i = 0; i < dev->nbufs; i++) {
        dev->bufs[i].ptr = malloc(FRAME_SIZE);
        if (!dev->bufs[i].ptr)
            return -1;
        dev->bufs[i].len = FRAME_SIZE;
        dev->bufs[i].queued = 0;
    }

    return 0;
}

static void handle_streamon(struct uvc_dev *dev)
{
    dev->shutdown = 0;
    dev->streaming = 0;
    dev->first_queued = 0;
    dev->qcnt = 0;
    dev->dqcnt = 0;

    if (alloc_userptr_bufs(dev) < 0) {
        printf("Failed allocating USERPTR buffers\n");
        return;
    }

    dev->next_push_ns = now_ns();
    printf("UVC stream armed\n");
}

static void handle_streamoff(struct uvc_dev *dev)
{
    if (dev->first_queued) {
        int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        ioctl(dev->fd, VIDIOC_STREAMOFF, &type);
    }

    struct v4l2_requestbuffers rb;
    CLEAR(rb);
    rb.count = 0;
    rb.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    rb.memory = V4L2_MEMORY_USERPTR;
    ioctl(dev->fd, VIDIOC_REQBUFS, &rb);

    free_userptr_bufs(dev);

    dev->streaming = 0;
    dev->first_queued = 0;
    dev->qcnt = 0;
    dev->dqcnt = 0;
    dev->next_push_ns = 0;

    printf("UVC streaming stopped\n");
}

static void process_uvc_event(struct uvc_dev *dev)
{
    struct v4l2_event ev;
    struct uvc_event *uev = (void *)&ev.u.data;
    struct uvc_request_data resp;

    if (ioctl(dev->fd, VIDIOC_DQEVENT, &ev) < 0)
        return;

    memset(&resp, 0, sizeof(resp));
    resp.length = -EL2HLT;

    switch (ev.type) {
    case UVC_EVENT_CONNECT: return;
    case UVC_EVENT_DISCONNECT: dev->shutdown = 1; return;
    case UVC_EVENT_SETUP: handle_setup(dev, &uev->req, &resp); break;
    case UVC_EVENT_DATA: handle_data(dev, &uev->data); return;
    case UVC_EVENT_STREAMON: handle_streamon(dev); return;
    case UVC_EVENT_STREAMOFF: handle_streamoff(dev); return;
    default: return;
    }

    ioctl(dev->fd, UVCIOC_SEND_RESPONSE, &resp);
}

static void process_uvc_dq(struct uvc_dev *dev)
{
    struct v4l2_buffer ubuf;
    CLEAR(ubuf);
    ubuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ubuf.memory = V4L2_MEMORY_USERPTR;

    while (ioctl(dev->fd, VIDIOC_DQBUF, &ubuf) == 0) {
        dev->dqcnt++;
        if (ubuf.index < dev->nbufs)
            dev->bufs[ubuf.index].queued = 0;
        CLEAR(ubuf);
        ubuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        ubuf.memory = V4L2_MEMORY_USERPTR;
    }
}

static void queue_next_frame(struct uvc_dev *dev)
{
    if (dev->nbufs == 0)
        return;

    if (!dev->shm.have_frame)
        return;

    uint64_t t = now_ns();
    if (t < dev->next_push_ns)
        return;

    unsigned idx = dev->nbufs;
    for (unsigned i = 0; i < dev->nbufs; i++) {
        if (!dev->bufs[i].queued) {
            idx = i;
            break;
        }
    }
    if (idx >= dev->nbufs)
        return;

    memcpy(dev->bufs[idx].ptr, dev->shm.latest_yuyv, FRAME_SIZE);

    struct v4l2_buffer ubuf;
    CLEAR(ubuf);
    ubuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ubuf.memory = V4L2_MEMORY_USERPTR;
    ubuf.index = idx;
    ubuf.m.userptr = (unsigned long)dev->bufs[idx].ptr;
    ubuf.length = FRAME_SIZE;
    ubuf.bytesused = FRAME_SIZE;

    if (ioctl(dev->fd, VIDIOC_QBUF, &ubuf) < 0)
        return;

    dev->bufs[idx].queued = 1;
    dev->qcnt++;

    if (!dev->first_queued) {
        int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        if (ioctl(dev->fd, VIDIOC_STREAMON, &type) == 0) {
            dev->first_queued = 1;
            dev->streaming = 1;
            printf("UVC streaming started\n");
        }
    }

    dev->next_push_ns = t + FRAME_NS;
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    const char *uvc_path = (argc > 1) ? argv[1] : "/dev/video0";

    struct uvc_dev dev;
    CLEAR(dev);
    dev.shm.fd = -1;
    dev.shm.latest_yuyv = malloc(FRAME_SIZE);
    if (!dev.shm.latest_yuyv)
        return 1;

    printf("UVC SHM bridge: %s <- %s (%dx%d YUY2 @%dfps)\n",
           uvc_path, SHM_PATH, WIDTH, HEIGHT, TARGET_FPS);

    if (uvc_open_dev(&dev, uvc_path) < 0)
        return 1;

    fill_streaming_ctrl(&dev.probe, 0);
    fill_streaming_ctrl(&dev.commit, 0);

    struct v4l2_event_subscription sub;
    CLEAR(sub);
    int events[] = { UVC_EVENT_SETUP, UVC_EVENT_DATA, UVC_EVENT_STREAMON, UVC_EVENT_STREAMOFF, 0 };
    for (int i = 0; events[i]; i++) {
        sub.type = events[i];
        ioctl(dev.fd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    }

    printf("Waiting for host...\n");

    while (1) {
        fd_set efds;
        FD_ZERO(&efds);
        FD_SET(dev.fd, &efds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 10000;

        select(dev.fd + 1, NULL, NULL, &efds, &tv);

        if (FD_ISSET(dev.fd, &efds))
            process_uvc_event(&dev);

        if (dev.nbufs > 0) {
            process_uvc_dq(&dev);
            shm_update_latest(&dev.shm);
            queue_next_frame(&dev);
        }
    }

    return 0;
}
