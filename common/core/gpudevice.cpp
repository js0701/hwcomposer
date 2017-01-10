/*
// Copyright (c) 2016 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include <gpudevice.h>

#include <stdlib.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <errno.h>

#include <linux/types.h>
#include <linux/netlink.h>

#include <hwctrace.h>

#include "displayplanemanager.h"
#include "headless.h"
#include "internaldisplay.h"
#include "virtualdisplay.h"
#include "pageflipeventhandler.h"
#include "pageflipstate.h"

namespace hwcomposer {

static void page_flip_event(int /*fd*/, unsigned int frame, unsigned int sec,
                            unsigned int usec, void *data) {
  IPAGEFLIPEVENTTRACE("page_flip_event called for frame %d", frame);
  PageFlipState *state = (PageFlipState *)data;
  if (!state)
    return;

  PageFlipEventHandler *event_handler = state->GetFlipHandler();

  IPAGEFLIPEVENTTRACE("Deleting page_flip_event state");
  delete state;
  IPAGEFLIPEVENTTRACE("Handling VBlank call back.");
  event_handler->HandlePageFlipEvent(sec, usec);
}

static void vblank_event(int /*fd*/, unsigned int /*frame*/,
                         unsigned int /*sec*/, unsigned int /*usec*/,
                         void * /*data*/) {
  IPAGEFLIPEVENTTRACE("vblank_event Called.");
}

GpuDevice::DisplayManager::DisplayManager() : HWCThread(-8) {
  CTRACE();
}

GpuDevice::DisplayManager::~DisplayManager() {
  CTRACE();
#ifdef UDEV_SUPPORT
  if (monitor_)
    udev_monitor_unref(monitor_);

  if (udev_)
    udev_unref(udev_);
#endif
}

bool GpuDevice::DisplayManager::Init(uint32_t fd) {
  CTRACE();
  fd_ = fd;
  int ret = drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
  if (ret) {
    ETRACE("Failed to set atomic cap %d", ret);
    return false;
  }

  ScopedDrmResourcesPtr res(drmModeGetResources(fd_));
  buffer_handler_.reset(NativeBufferHandler::CreateInstance(fd_));
  if (!buffer_handler_) {
    ETRACE("Failed to create native buffer handler instance");
    return false;
  }

  for (int32_t i = 0; i < res->count_crtcs; ++i) {
    ScopedDrmCrtcPtr c(drmModeGetCrtc(fd_, res->crtcs[i]));
    if (!c) {
      ETRACE("Failed to get crtc %d", res->crtcs[i]);
      return false;
    }

    std::unique_ptr<NativeDisplay> display(
        new InternalDisplay(fd_, *(buffer_handler_.get()), i, c->crtc_id));
    if (!display->Initialize()) {
      ETRACE("Failed to Initialize Display %d", c->crtc_id);
      return false;
    }

    displays_.emplace_back(std::move(display));
  }

  virtual_display_.reset(
      new VirtualDisplay(fd_, *(buffer_handler_.get()), 0, 0));

  if (!UpdateDisplayState()) {
    ETRACE("Failed to connect display.");
    return false;
  }
#ifdef UDEV_SUPPORT
  udev_ = udev_new();
  if (udev_ == NULL) {
    ETRACE("Failed to create udev. %s", PRINTERROR());
    return true;
  }

  monitor_ = udev_monitor_new_from_netlink(udev, "udev");
  if (monitor_ == NULL) {
    ETRACE("Failed to create udev monitor. %s", PRINTERROR());
    udev_unref(udev_);
    return true;
  }

  if (udev_monitor_filter_add_match_subsystem_devtype(monitor_, "drm",
                                                      "drm_minor") < 0) {
    ETRACE("Failed to add drm filter for udev monitor. %s", PRINTERROR());
    udev_unref(udev_);
    udev_monitor_unref(monitor_);
    return true;
  }
  if (udev_monitor_filter_update(monitor_) < 0) {
    ETRACE("udev_monitor_filter_update failed. %s", PRINTERROR());
    udev_unref(udev_);
    udev_monitor_unref(monitor_);
    return true;
  }

  if (udev_monitor_enable_receiving(monitor_) < 0) {
    ETRACE("Failed to enable udev monitor. %s", PRINTERROR());
    udev_unref(udev_);
    udev_monitor_unref(monitor_);
    return true;
  }

  hotplug_fd_.Reset(udev_monitor_get_fd(monitor));
  if (hotplug_fd_ < 0) {
    ETRACE("Failed to retrieve udev monitor fd. %s", PRINTERROR());
    udev_unref(udev_);
    udev_monitor_unref(monitor_);
    return true;
  }
#else
  hotplug_fd_.Reset(socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT));
  if (hotplug_fd_.get() < 0) {
    ETRACE("Failed to create socket for hot plug monitor. %s", PRINTERROR());
    return true;
  }

  struct sockaddr_nl addr;
  memset(&addr, 0, sizeof(addr));
  addr.nl_family = AF_NETLINK;
  addr.nl_pid = getpid();
  addr.nl_groups = -1;

  ret = bind(hotplug_fd_.get(), (struct sockaddr *)&addr, sizeof(addr));
  if (ret) {
    ETRACE("Failed to bind sockaddr_nl and hot plug monitor fd. %s",
           PRINTERROR());
    return true;
  }
#endif
  FD_ZERO(&fd_set_);
  FD_SET(hotplug_fd_.get(), &fd_set_);
  FD_SET(fd_, &fd_set_);
  select_fd_ = std::max(hotplug_fd_.get(), fd_) + 1;
  IHOTPLUGEVENTTRACE(
      "Initialization of ueventd/udev succeeded. Initializing worker thread.");
  if (!InitWorker("DisplayManager")) {
    ETRACE("Failed to initalizer thread to monitor Hot Plug events. %s",
           PRINTERROR());
  }

  return true;
}

#ifdef UDEV_SUPPORT
void GpuDevice::DisplayManager::HotPlugEventHandler() {
  CTRACE();
  struct udev_device *dev;
  struct stat s;
  dev_t udev_devnum;
  const char *hotplug;

  dev = udev_monitor_receive_device(monitor_);
  if (!dev) {
    ETRACE("Failed to retrieve udev device. %s", PRINTERROR());
    IHOTPLUGEVENTTRACE(
        "Display management not possible as we failed to retrieve udev "
        "device.");
    return;
  }

  udev_devnum = udev_device_get_devnum(dev);
  fstat(fd_, &s);

  hotplug = udev_device_get_property_value(dev, "HOTPLUG");

  if (memcmp(&s.st_rdev, &udev_devnum, sizeof(dev_t)) == 0 && hotplug &&
      atoi(hotplug) == 1)
    UpdateDisplayState();

  udev_device_unref(dev);
}
#else
void GpuDevice::DisplayManager::HotPlugEventHandler() {
  CTRACE();
  char buffer[1024];
  int ret;
  bool drm_event = false, hotplug_event = false;

  while (true) {
    ret = read(hotplug_fd_.get(), &buffer, sizeof(buffer));
    if (ret == 0) {
      return;
    } else if (ret < 0) {
      ETRACE("Failed to read uevent. %s", PRINTERROR());
      IHOTPLUGEVENTTRACE(
          "Display management not possible as we failed to read uevent.");
      return;
    }

    if (drm_event && hotplug_event)
      continue;

    for (int32_t i = 0; i < ret;) {
      char *event = buffer + i;
      if (strcmp(event, "DEVTYPE=drm_minor"))
        drm_event = true;
      else if (strcmp(event, "HOTPLUG=1"))
        hotplug_event = true;

      if (hotplug_event && drm_event)
        break;

      i += strlen(event) + 1;
    }

    if (drm_event && hotplug_event) {
      IHOTPLUGEVENTTRACE(
          "Recieved Hot Plug event related to display calling "
          "UpdateDisplayState.");
      UpdateDisplayState();
    }
  }
}
#endif

void GpuDevice::DisplayManager::Routine() {
  CTRACE();
  int ret;
  IHOTPLUGEVENTTRACE("DisplayManager::Routine.");
  do {
    ret = select(select_fd_, &fd_set_, NULL, NULL, NULL);
  } while (ret == -1 && errno == EINTR);

  if (ret < 0) {
    IHOTPLUGEVENTTRACE("select() failed with %s:", PRINTERROR());
  } else if (FD_ISSET(0, &fd_set_)) {
    IHOTPLUGEVENTTRACE("select() exit due to user-input.");
  } else {
    if (FD_ISSET(hotplug_fd_.get(), &fd_set_)) {
      IHOTPLUGEVENTTRACE("Recieved Hot plug notification.");
      HotPlugEventHandler();
    }

    if (FD_ISSET(fd_, &fd_set_)) {
      IPAGEFLIPEVENTTRACE("drmHandleEvent recieved.");
      drmEventContext event_context = {.version = DRM_EVENT_CONTEXT_VERSION,
                                       .vblank_handler = vblank_event,
                                       .page_flip_handler = page_flip_event};
      drmHandleEvent(fd_, &event_context);
    }
  }
}

bool GpuDevice::DisplayManager::UpdateDisplayState() {
  CTRACE();
  ScopedDrmResourcesPtr res(drmModeGetResources(fd_));
  if (!res) {
    ETRACE("Failed to get DrmResources resources");
    return false;
  }

  int ret = 0;
  if (initialized_) {
    ret = Lock();
    if (ret)
      ETRACE("Failed to lock in GetDisplay %d", ret);
  }

  // Start of assuming no displays are connected
  for (auto &display : displays_) {
    display->DisConnect();
  }

  for (int32_t i = 0; i < res->count_connectors; ++i) {
    ScopedDrmConnectorPtr connector(
        drmModeGetConnector(fd_, res->connectors[i]));
    if (!connector) {
      ETRACE("Failed to get connector %d", res->connectors[i]);
      if (initialized_) {
        ret = Unlock();
        if (ret)
          ETRACE("Failed to unlock in GetDisplay %d", ret);
      }
      return false;
    }

    // check if a monitor is connected.
    if (connector->connection != DRM_MODE_CONNECTED)
      continue;

    // Ensure we have atleast one valid mode.
    if (connector->count_modes == 0)
      continue;

    drmModeModeInfo mode;
    for (int32_t i = 0; i < connector->count_modes; ++i) {
      mode = connector->modes[i];
      // There is only one preferred mode per connector.
      if (mode.type & DRM_MODE_TYPE_PREFERRED) {
        break;
      }
    }

    if (!(mode.type & DRM_MODE_TYPE_PREFERRED))
      continue;

    // Lets try to find crts for any connected encoder.
    if (connector->encoder_id) {
      ScopedDrmEncoderPtr encoder(
          drmModeGetEncoder(fd_, connector->encoder_id));
      if (encoder && encoder->crtc_id) {
        for (auto &display : displays_) {
          if (encoder->crtc_id == display->CrtcId() &&
              display->Connect(mode, connector)) {
            break;
          }
        }
      }
    }

    // Try to find an encoder for the connector.
    for (int32_t i = 0; i < connector->count_encoders; ++i) {
      ScopedDrmEncoderPtr encoder(
          drmModeGetEncoder(fd_, connector->encoders[i]));
      if (!encoder)
        continue;

      // Check for compatible CRTC
      int crtc_bit = 1 << i;
      if (!(encoder->possible_crtcs & crtc_bit))
        continue;

      for (auto &display : displays_) {
        if (!display->IsConnected() && (1 << display->Pipe() & crtc_bit) &&
            display->Connect(mode, connector)) {
          break;
        }
      }
    }
  }

  bool headless_mode = true;
  for (auto &display : displays_) {
    if (!display->IsConnected()) {
      display->ShutDown();
    } else {
      headless_mode = false;
    }
  }

  if (headless_mode) {
    if (!headless_)
      headless_.reset(new Headless(fd_, *(buffer_handler_.get()), 0, 0));
  } else if (headless_) {
    headless_.release();
  }

  if (initialized_) {
    ret = Unlock();
    if (ret)
      ETRACE("Failed to unlock in GetDisplay %s", PRINTERROR());
  }

  return true;
}

NativeDisplay *GpuDevice::DisplayManager::GetDisplay(uint32_t display_id) {
  CTRACE();
  int ret = Lock();
  if (ret)
    ETRACE("Failed to lock in GetDisplay %s", PRINTERROR());

  NativeDisplay *display = NULL;
  if (headless_.get()) {
    display = headless_.get();
  } else {
    for (auto &target : displays_) {
      if (target->Pipe() == display_id) {
        display = target.get();
        break;
      }
    }
  }

  ret = Unlock();
  if (ret)
    ETRACE("Failed to unlock in GetDisplay %s", PRINTERROR());

  return display;
}

NativeDisplay *GpuDevice::DisplayManager::GetVirtualDisplay() {
  return virtual_display_.get();
}

GpuDevice::GpuDevice() : initialized_(false) {
  CTRACE();
}

GpuDevice::~GpuDevice() {
  CTRACE();
}

bool GpuDevice::Initialize() {
  CTRACE();
  if (initialized_)
    return true;

  fd_.Reset(drmOpen("i915", NULL));
  if (fd_.get() < 0) {
    ETRACE("Failed to open dri %s", PRINTERROR());
    return -ENODEV;
  }

  struct drm_set_client_cap cap = {DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1};
  drmIoctl(fd_.get(), DRM_IOCTL_SET_CLIENT_CAP, &cap);
  int ret = drmSetClientCap(fd_.get(), DRM_CLIENT_CAP_ATOMIC, 1);
  if (ret) {
    ETRACE("Failed to set atomic cap %s", PRINTERROR());
    return false;
  }
  ScopedDrmResourcesPtr res(drmModeGetResources(fd_.get()));

  initialized_ = true;

  return display_manager_.Init(fd_.get());
}

NativeDisplay *GpuDevice::GetDisplay(uint32_t display_id) {
  return display_manager_.GetDisplay(display_id);
}

NativeDisplay *GpuDevice::GetVirtualDisplay() {
  return display_manager_.GetVirtualDisplay();
}

}  // namespace hwcomposer
