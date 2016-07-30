/*
**
** Copyright 2008, The Android Open Source Project
** Copyright 2010, Moko365 Inc.
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

#define LOG_TAG "CameraHardware"
#include <utils/Log.h>

#include "CameraHardware.h"
#include <utils/threads.h>
#include <fcntl.h>
#include <sys/mman.h>

namespace android {

CameraHardware::CameraHardware()
                  : mParameters(),
                    mPreviewHeap(0),
                    mRawHeap(0),
		    mCamera(0),
                    mPreviewFrameSize(0),
                    mRawPictureCallback(0),
                    mJpegPictureCallback(0),
                    mPictureCallbackCookie(0),
                    mPreviewCallback(0),
                    mPreviewCallbackCookie(0),
                    mAutoFocusCallback(0),
                    mAutoFocusCallbackCookie(0),
                    mCurrentPreviewFrame(0)
{
    initDefaultParameters();
}

void CameraHardware::initDefaultParameters()
{
    CameraParameters p;

    p.setPreviewSize(160, 120);
    p.setPreviewFrameRate(15);
    p.setPreviewFormat("rgb565");

    p.setPictureSize(640, 480);
    p.setPictureFormat("rgb565");

    if (setParameters(p) != NO_ERROR) {
        LOGE("Failed to set default parameters?!");
    } 
}

void CameraHardware::initHeapLocked()
{
    // Create raw heap. Format is rgb565 (640x480) -- use vivi emulator
    int picture_width, picture_height;
    mParameters.getPictureSize(&picture_width, &picture_height);
    mRawHeap = new MemoryHeapBase(picture_width * 2 * picture_height);

    int preview_width, preview_height;
    mParameters.getPreviewSize(&preview_width, &preview_height);
    LOGD("initHeapLocked: preview size=%dx%d", preview_width, preview_height);

    // Note that we enforce yuv422 in setParameters().
    int how_big = preview_width * preview_height * 2;

    // If we are being reinitialized to the same size as before, no
    // work needs to be done.
    if (how_big == mPreviewFrameSize)
        return;

    mPreviewFrameSize = how_big;

    // Make a new mmap'ed heap that can be shared across processes. 
    // use code below to test with pmem
    mPreviewHeap = new MemoryHeapBase(mPreviewFrameSize * kBufferCount);
    // Make an IMemory for each frame so that we can reuse them in callbacks.
    for (int i = 0; i < kBufferCount; i++) {
        mBuffers[i] = new MemoryBase(mPreviewHeap, i * mPreviewFrameSize, mPreviewFrameSize);
    }
}

CameraHardware::~CameraHardware()
{
    delete mCamera;
    mCamera = 0;
    singleton.clear();
}

sp<IMemoryHeap> CameraHardware::getPreviewHeap() const
{
    return mPreviewHeap;
}

sp<IMemoryHeap> CameraHardware::getRawHeap() const
{
    return mRawHeap;
}

// ---------------------------------------------------------------------------

int CameraHardware::previewThread()
{
    mLock.lock();
        // the attributes below can change under our feet...

        int previewFrameRate = mParameters.getPreviewFrameRate();

        // Find the offset within the heap of the current buffer.
        ssize_t offset = mCurrentPreviewFrame * mPreviewFrameSize;

        sp<MemoryHeapBase> heap = mPreviewHeap;
    
        sp<MemoryBase> buffer = mBuffers[mCurrentPreviewFrame];
        
    mLock.unlock();

    // TODO: here check all the conditions that could go wrong
    if (buffer != 0) {
        // This is always valid, even if the client died -- the memory
        // is still mapped in our process.
        void *base = heap->base();
    
        // Fill the current frame with the fake camera.
        uint8_t *frame = ((uint8_t *)base) + offset;
    
	// V4L2: copy frame
	mCamera->GrabRawFrame(frame);

        //LOGV("previewThread: generated frame to buffer %d", mCurrentPreviewFrame);
        // Notify the client of a new frame.
        mPreviewCallback(buffer, mPreviewCallbackCookie);
    
        // Advance the buffer pointer.
        mCurrentPreviewFrame = (mCurrentPreviewFrame + 1) % kBufferCount;
    }

    return NO_ERROR;
}

status_t CameraHardware::startPreview(preview_callback cb, void* user)
{
    Mutex::Autolock lock(mLock);
    if (mPreviewThread != 0) {
        // already running
        return INVALID_OPERATION;
    }
    LOGD("startPreview: starting preview");

    if (!mCamera) {
	delete mCamera;
	mCamera = new V4L2Camera();
    }

    if (mCamera->Open("/dev/video0", 640, 480, V4L2_PIX_FMT_RGB565) < 0) {
        LOGE("startPreview: cannot open videodev");
	return UNKNOWN_ERROR;
    }

    if (mCamera->Init() < 0) {
        LOGE("startPreview: init videodev failed, %s", strerror(errno));
	return UNKNOWN_ERROR;
    }

    mCamera->StartStreaming();

    mPreviewCallback = cb;
    mPreviewCallbackCookie = user;
    mPreviewThread = new PreviewThread(this);
    return NO_ERROR;
}

void CameraHardware::stopPreview()
{
    sp<PreviewThread> previewThread;
    
    { // scope for the lock
        Mutex::Autolock lock(mLock);
        previewThread = mPreviewThread;
    }

    if (previewThread != 0) {
	mCamera->StopStreaming();
	mCamera->Uninit();
	mCamera->Close();
    }

    // don't hold the lock while waiting for the thread to quit
    if (previewThread != 0) {
        previewThread->requestExitAndWait();
    }

    Mutex::Autolock lock(mLock);
    mPreviewThread.clear();
}

bool CameraHardware::previewEnabled() {
    return mPreviewThread != 0;
}

status_t CameraHardware::startRecording(recording_callback cb, void* user)
{
    return UNKNOWN_ERROR;
}

void CameraHardware::stopRecording()
{
}

bool CameraHardware::recordingEnabled()
{
    return false;
}

void CameraHardware::releaseRecordingFrame(const sp<IMemory>& mem)
{
}

// ---------------------------------------------------------------------------

int CameraHardware::beginAutoFocusThread(void *cookie)
{
    CameraHardware *c = (CameraHardware *)cookie;
    return c->autoFocusThread();
}

int CameraHardware::autoFocusThread()
{
    if (mAutoFocusCallback != NULL) {
        mAutoFocusCallback(true, mAutoFocusCallbackCookie);
        mAutoFocusCallback = NULL;
        return NO_ERROR;
    }
    return UNKNOWN_ERROR;
}

status_t CameraHardware::autoFocus(autofocus_callback af_cb,
                                       void *user)
{
    Mutex::Autolock lock(mLock);

    if (mAutoFocusCallback != NULL) {
        return mAutoFocusCallback == af_cb ? NO_ERROR : INVALID_OPERATION;
    }

    mAutoFocusCallback = af_cb;
    mAutoFocusCallbackCookie = user;
    if (createThread(beginAutoFocusThread, this) == false)
        return UNKNOWN_ERROR;
    return NO_ERROR;
}

/*static*/ int CameraHardware::beginPictureThread(void *cookie)
{
    CameraHardware *c = (CameraHardware *)cookie;
    return c->pictureThread();
}

int CameraHardware::pictureThread()
{
    mLock.lock();
        sp<MemoryBase> mem = mBuffers[mCurrentPreviewFrame];
    mLock.unlock();

    if (mShutterCallback)
        mShutterCallback(mPictureCallbackCookie);

    if (mRawPictureCallback) {
        int w, h;
        mParameters.getPictureSize(&w, &h);
        //sp<MemoryBase> mem = new MemoryBase(mRawHeap, 0, w * 2 * h);

        if (mRawPictureCallback)
            mRawPictureCallback(mem, mPictureCallbackCookie);
    }

    if (mJpegPictureCallback) {
    }
    return NO_ERROR;
}

status_t CameraHardware::takePicture(shutter_callback shutter_cb,
                                         raw_callback raw_cb,
                                         jpeg_callback jpeg_cb,
                                         void* user)
{
    stopPreview();
    mShutterCallback = shutter_cb;
    mRawPictureCallback = raw_cb;
    mJpegPictureCallback = jpeg_cb;
    mPictureCallbackCookie = user;
    if (createThread(beginPictureThread, this) == false)
        return -1;
    return NO_ERROR;
}

status_t CameraHardware::cancelPicture(bool cancel_shutter,
                                           bool cancel_raw,
                                           bool cancel_jpeg)
{
    if (cancel_shutter) mShutterCallback = NULL;
    if (cancel_raw) mRawPictureCallback = NULL;
    if (cancel_jpeg) mJpegPictureCallback = NULL;
    return NO_ERROR;
}

status_t CameraHardware::dump(int fd, const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    AutoMutex lock(&mLock);

    snprintf(buffer, 255, " preview frame(%d), size (%d), running(%s)\n", mCurrentPreviewFrame, mPreviewFrameSize, mPreviewRunning?"true": "false");

    result.append(buffer);
    write(fd, result.string(), result.size());

    return NO_ERROR;
}

status_t CameraHardware::setParameters(const CameraParameters& params)
{
    Mutex::Autolock lock(mLock);

    if (strcmp(params.getPreviewFormat(), "rgb565") != 0) {
        LOGE("Only rgb565 preview is supported");
        return -1;
    }

    if (strcmp(params.getPictureFormat(), "rgb565") != 0) {
        LOGE("Only rgb565 still pictures are supported");
        return -1;
    }

    mParameters = params;

    initHeapLocked();

    return NO_ERROR;
}

CameraParameters CameraHardware::getParameters() const
{
    Mutex::Autolock lock(mLock);
    return mParameters;
}

void CameraHardware::release()
{
}

wp<CameraHardwareInterface> CameraHardware::singleton;

sp<CameraHardwareInterface> CameraHardware::createInstance()
{
    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            return hardware;
        }
    }
    sp<CameraHardwareInterface> hardware(new CameraHardware());
    singleton = hardware;
    return hardware;
}

extern "C" sp<CameraHardwareInterface> openCameraHardware()
{
    return CameraHardware::createInstance();
}

}; // namespace android
