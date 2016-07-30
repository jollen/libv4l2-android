LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:=               \
    CameraHardware.cpp      \
    v4l2/V4L2Camera.cpp

LOCAL_MODULE:= libcamera

LOCAL_SHARED_LIBRARIES:= \
    libui \
    libutils \
    libcutils \
    libmedia

include $(BUILD_SHARED_LIBRARY)
