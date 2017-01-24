/*
 * Copyright (c) 2012 Arvin Schnell <arvin.schnell@gmail.com>
 * Copyright (c) 2012 Rob Clark <rob@ti.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* Based on a egl cube test app originally written by Arvin Schnell */

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <drm_fourcc.h>

#include "esUtil.h"
#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>

#include <libsync.h>

#include <gpudevice.h>
#include <hwclayer.h>
#include <nativedisplay.h>
#include <platformdefines.h>
#include <nativefence.h>
#include <spinlock.h>

#include <kmscubelayerrenderer.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Exit after rendering the given number of frames. If 0, then continue
 * rendering forever.
 */
static uint64_t arg_frames = 0;

struct frame {
  hwcomposer::HwcLayer layer;
  LayerRenderer* layer_renderer;
  // NativeFence release_fence;
};

static struct frame frames[2];

class HotPlugEventCallback : public hwcomposer::DisplayHotPlugEventCallback {
 public:
  HotPlugEventCallback(hwcomposer::GpuDevice *device) : device_(device) {
  }

  void Callback(std::vector<hwcomposer::NativeDisplay *> connected_displays) {
    hwcomposer::ScopedSpinLock lock(spin_lock_);
    connected_displays_.swap(connected_displays);
  }

  const std::vector<hwcomposer::NativeDisplay *> &GetConnectedDisplays() {
    hwcomposer::ScopedSpinLock lock(spin_lock_);
    if (connected_displays_.empty())
      connected_displays_ = device_->GetConnectedPhysicalDisplays();

    return connected_displays_;
  }

 private:
  std::vector<hwcomposer::NativeDisplay *> connected_displays_;
  hwcomposer::GpuDevice *device_;
  hwcomposer::SpinLock spin_lock_;
};

static struct { struct gbm_device *dev; } gbm;

struct drm_fb {
  struct gbm_bo *bo;
};

static int init_gbm(int fd) {
  gbm.dev = gbm_create_device(fd);
  if (!gbm.dev) {
    printf("failed to create gbm device\n");
    return -1;
  }

  return 0;
}

static void init_frames(int32_t width, int32_t height) {
  for (int i = 0; i < ARRAY_SIZE(frames); ++i) {
    struct frame *frame = &frames[i];
    // now we need to parse the configuration from json
    frame->layer_renderer = new KMSCubeLayerRenderer(gbm.dev);
    if(!frame->layer_renderer->Init(width,  height,GBM_FORMAT_XRGB8888))
	{
	    printf("LayerRenderer failed to initialize!\n");
		exit(-1);
	}
    frame->layer.SetTransform(0);
    frame->layer.SetSourceCrop(hwcomposer::HwcRect<float>(0, 0, width, height));
    frame->layer.SetDisplayFrame(hwcomposer::HwcRect<int>(0, 0, width, height));
    frame->layer.SetNativeHandle(frame->layer_renderer->GetNativeBoHandle());
  }
}

static void print_help(void) {
  printf("usage: kmscube [-h|--help] [-f|--frames <frames>]\n");
}

static void parse_args(int argc, char *argv[]) {
  static const struct option longopts[] = {
      {"help", no_argument, NULL, 'h'},
      {"frames", required_argument, NULL, 'f'},
      {0},
  };

  char *endptr;
  int opt;
  int longindex = 0;

  /* Suppress getopt's poor error messages */
  opterr = 0;

  while ((opt = getopt_long(argc, argv, "+:hf:", longopts,
                            /*longindex*/ &longindex)) != -1) {
    switch (opt) {
      case 'h':
        print_help();
        exit(0);
        break;
      case 'f':
        errno = 0;
        arg_frames = strtoul(optarg, &endptr, 0);
        if (errno || *endptr != '\0') {
          fprintf(stderr, "usage error: invalid value for <frames>\n");
          exit(EXIT_FAILURE);
        }
        break;
      case ':':
        fprintf(stderr, "usage error: %s requires an argument\n",
                argv[optind - 1]);
        exit(EXIT_FAILURE);
        break;
      case '?':
      default:
        assert(opt == '?');
        fprintf(stderr, "usage error: unknown option '%s'\n", argv[optind - 1]);
        exit(EXIT_FAILURE);
        break;
    }
  }

  if (optind < argc) {
    fprintf(stderr, "usage error: trailing args\n");
    exit(EXIT_FAILURE);
  }
}

int main(int argc, char *argv[]) {
  struct drm_fb *fb;
  int ret, fd, primary_width, primary_height;
  hwcomposer::GpuDevice device;
  device.Initialize();
  auto callback = std::make_shared<HotPlugEventCallback>(&device);
  device.RegisterHotPlugEventCallback(callback);
  const std::vector<hwcomposer::NativeDisplay *> &displays =
      callback->GetConnectedDisplays();
  if (displays.empty())
    return 0;

  parse_args(argc, argv);
  fd = open("/dev/dri/renderD128", O_RDWR);
  primary_width = displays.at(0)->Width();
  primary_height = displays.at(0)->Height();

  ret = init_gbm(fd);
  if (ret) {
    printf("failed to initialize GBM\n");
    close(fd);
    return ret;
  }

  init_frames(primary_width, primary_height);

  /* clear the color buffer */
  int64_t gpu_fence_fd = -1; /* out-fence from gpu, in-fence to kms */
  std::vector<hwcomposer::HwcLayer *> layers;

  struct frame *frame_old = NULL;

  for (uint64_t i = 1; arg_frames == 0 || i < arg_frames; ++i) {
    struct frame *frame = &frames[i % ARRAY_SIZE(frames)];

    /*
     * Wait on the fence from the previous frame, since the current one was not
     * submitted yet and thus has no valid fence.
     */
    if (frame_old && frame_old->layer.release_fence.get() != -1) {
      ret = sync_wait(frame_old->layer.release_fence.get(), 1000);
      frame_old->layer.release_fence.Reset(-1);
      if (ret) {
        printf("failed waiting on sync fence: %s\n", strerror(errno));
        return -1;
      }
    }

    frame->layer_renderer->Draw(&gpu_fence_fd);
    frame->layer.acquire_fence = gpu_fence_fd;

    std::vector<hwcomposer::HwcLayer *>().swap(layers);
    layers.emplace_back(&frame->layer);

    const std::vector<hwcomposer::NativeDisplay *> &displays =
        callback->GetConnectedDisplays();
    if (displays.empty())
      return 0;

    for (auto &display : displays)
      display->Present(layers);

    frame_old = frame;
  }

  close(fd);
  return ret;
}
