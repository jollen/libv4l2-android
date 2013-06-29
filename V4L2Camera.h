/*
**
** Copyright (C) 2010 Moko365 Inc.
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

#ifndef _CAMIF_H_
#define _CAMIF_H_

#include <linux/videodev2.h>

namespace android {

class V4L2Camera {

public:
    V4L2Camera();
    ~V4L2Camera();

    int Open(const char *device,
	     unsigned int width,
	     unsigned int height,
	     unsigned int pixelformat);
    int Init();
    void Uninit();
    void Close();

    void StartStreaming();
    void StopStreaming();

    void GrabRawFrame(void *raw_base);
    void Convert(void *raw_base,
		 void *preview_base,
		 unsigned int ppnum);

private:
    int fd;
    int start;
    unsigned char *mem;
    struct v4l2_buffer buf;

    unsigned int width;
    unsigned int height;
    unsigned int pixelformat;
};

}; // namespace

#endif
