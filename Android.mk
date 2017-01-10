# Copyright (C) 2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ifeq ($(strip $(BOARD_USES_DRM_HWCOMPOSER)),true)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libdrm \
	libEGL \
	libGLESv2 \
	libhardware \
	liblog \
	libsync \
	libui \
	libutils


LOCAL_C_INCLUDES := \
        vendor/intel/external/android_ia/drm_gralloc \
	vendor/intel/external/android_ia/libdrm \
	vendor/intel/external/android_ia/libdrm/include/drm \
	system/core/include/utils \
	system/core/libsync \
	system/core/libsync/include \
	vendor/intel/external/android_ia/hwcomposer/public \
	vendor/intel/external/android_ia/hwcomposer/common/core \
	vendor/intel/external/android_ia/hwcomposer/common/compositor \
	vendor/intel/external/android_ia/hwcomposer/common/compositor/gl \
	vendor/intel/external/android_ia/hwcomposer/common/display \
	vendor/intel/external/android_ia/hwcomposer/common/utils \
	vendor/intel/external/android_ia/hwcomposer/common/watchers \
	vendor/intel/external/android_ia/hwcomposer/os/android \

LOCAL_SRC_FILES := \
	common/compositor/compositor.cpp \
	common/compositor/factory.cpp \
	common/compositor/nativesurface.cpp \
	common/compositor/renderstate.cpp \
	common/compositor/scopedrendererstate.cpp \
	common/core/headless.cpp \
	common/core/internaldisplay.cpp \
	common/core/virtualdisplay.cpp \
	common/core/gpudevice.cpp \
	common/core/nativesync.cpp \
	common/core/overlaylayer.cpp \
	common/display/displayplane.cpp \
	common/display/displayplanemanager.cpp \
	common/display/overlaybuffer.cpp \
	common/display/pageflipeventhandler.cpp \
	common/display/pageflipstate.cpp \
	common/utils/separate_rects.cpp \
	common/utils/hwcthread.cpp \
	os/android/grallocbufferhandler.cpp \
	os/android/drmhwctwo.cpp \
	public/drmscopedtypes.cpp \
	public/hwclayer.cpp \

LOCAL_CPPFLAGS += \
	-DHWC2_USE_CPP11 \
	-DHWC2_INCLUDE_STRINGIFICATION \
	-DUSE_ANDROID_SYNC

ifeq ($(strip $(BOARD_DISABLE_OVERLAY_USAGE)),true)
LOCAL_CPPFLAGS += -DDISABLE_OVERLAY_USAGE
endif

ifeq ($(strip $(BOARD_DISABLE_EXPLICIT_SYNC)),true)
LOCAL_CPPFLAGS += -DDISABLE_EXPLICIT_SYNC
endif

ifeq ($(strip $(BOARD_USES_VULKAN)),)
LOCAL_CPPFLAGS += \
	-DUSE_GL

LOCAL_SRC_FILES += \
	common/compositor/gl/glprogram.cpp \
	common/compositor/gl/glrenderer.cpp \
	common/compositor/gl/glsurface.cpp \
	common/compositor/gl/egloffscreencontext.cpp \
	common/compositor/gl/nativeglresource.cpp
endif

LOCAL_MODULE := hwcomposer.android_ia
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := $(TARGET_SHLIB_SUFFIX)
include $(BUILD_SHARED_LIBRARY)

endif
