/*
**
** Copyright (C) 2010 Moko365 Inc
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define	LOG_TAG	"V4LCAMERA"
#include <utils/Log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/videodev2.h>

#include <ui/PixelFormat.h>

#include "V4L2Camera.h"

namespace android {

V4L2Camera::V4L2Camera()
	: start(0)
{
}

V4L2Camera::~V4L2Camera()
{
}

int V4L2Camera::Open(const char *filename,
                      unsigned int w,
                      unsigned int h,
                      unsigned int p)
{
    int ret;
    struct v4l2_format format;

    fd = open(filename, O_RDWR);
    if (fd < 0) {
        LOGE("Error opening device: %s", filename);
        return -1;
    }

    width = w;
    height = h;
    pixelformat = p;

    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = pixelformat;

    // MUST set 
    format.fmt.pix.field = V4L2_FIELD_ANY;

    ret = ioctl(fd, VIDIOC_S_FMT, &format);
    if (ret < 0) {
        LOGE("Unable to set format: %s", strerror(errno));
        return -1;
    }

    return 0;
}

void V4L2Camera::Close()
{
    close(fd);
}

int V4L2Camera::Init()
{
    int ret;
    struct v4l2_requestbuffers rb;

    start = false;

    /* V4L2: request buffers, only 1 frame */
    rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rb.memory = V4L2_MEMORY_MMAP;
    rb.count = 1;

    ret = ioctl(fd, VIDIOC_REQBUFS, &rb);
    if (ret < 0) {
        LOGE("Unable request buffers: %s", strerror(errno));
        return -1;
    }

    /* V4L2: map buffer  */
    memset(&buf, 0, sizeof(struct v4l2_buffer));

    buf.index = 0;
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(fd, VIDIOC_QUERYBUF, &buf);
    if (ret < 0) {
        LOGE("Unable query buffer: %s", strerror(errno));
        return -1;
    }

    /* Only map one */
    mem = (unsigned char *)mmap(0, buf.length, PROT_READ | PROT_WRITE, 
				MAP_SHARED, fd, buf.m.offset);
    if (mem == MAP_FAILED) {
        LOGE("Unable map buffer: %s", strerror(errno));
        return -1;
    }

    /* V4L2: queue buffer */
    ret = ioctl(fd, VIDIOC_QBUF, &buf);

    return 0;
}

void V4L2Camera::Uninit()
{
    munmap(mem, buf.length);
    return ;
}

void V4L2Camera::StartStreaming()
{
    enum v4l2_buf_type type;
    int ret;

    if (start) return;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        LOGE("Unable query buffer: %s", strerror(errno));
        return;
    }

    start = true;
}

void V4L2Camera::StopStreaming()
{
    enum v4l2_buf_type type;
    int ret;

    if (!start) return;

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        LOGE("Unable query buffer: %s", strerror(errno));
        return;
    }

    start = false;
}

void V4L2Camera::GrabRawFrame(void *raw_base)
{
    int ret;

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    /* V4L2: dequeue buffer */
    ret = ioctl(fd, VIDIOC_DQBUF, &buf);
    if (ret < 0) {
        LOGE("Unable query buffer: %s", strerror(errno));
        return;
    }

    /* copy to userspace */
    memcpy((unsigned char *)raw_base, mem, buf.bytesused);

    /* V4l2: queue buffer again after that */
    ret = ioctl(fd, VIDIOC_QBUF, &buf);
    if (ret < 0) {
        LOGE("Unable query buffer: %s", strerror(errno));
        return;
    }
}

void V4L2Camera::Convert(void *r, void *p, unsigned int ppm)
{
    unsigned char *raw = (unsigned char *)r;
    unsigned char *preview = (unsigned char *)p;

    /* We don't need to really convert that */
    if (pixelformat == PIXEL_FORMAT_RGB_888) {
        /* copy to preview buffer */
        memcpy(preview, raw, width*height*ppm);
    }

    /* TODO: Convert YUV to RGB. */

    return;
}


}; // namespace
