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

#include "gllayerrenderer.h"
#include <assert.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

GLLayerRenderer::GLLayerRenderer(struct gbm_device *dev)
                                :LayerRenderer(dev)
{

}

GLLayerRenderer::~GLLayerRenderer()
{

}

bool GLLayerRenderer::Init(uint32_t width, uint32_t height, uint32_t format)
{
   if(format != GBM_FORMAT_XRGB8888) return false;
   if(!LayerRenderer::Init(width, height, format)) return false;

   EGLint major, minor, n;
   static const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 3,
                                           EGL_NONE};

   static const EGLint config_attribs[] = {EGL_SURFACE_TYPE, EGL_DONT_CARE,
                                          EGL_NONE};
										  
   gl_.display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                     EGL_DEFAULT_DISPLAY, NULL);

   if (!eglInitialize(gl_.display, &major, &minor)) {
     printf("failed to initialize EGL\n");
     return false;
   }

#define get_proc(name, proc)                  \
  do {                                        \
    gl_.name = (proc)eglGetProcAddress(#name); \
    assert(gl_.name);                          \
  } while (0)
  get_proc(glEGLImageTargetRenderbufferStorageOES,
           PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC);
  get_proc(eglCreateImageKHR, PFNEGLCREATEIMAGEKHRPROC);
  get_proc(eglCreateSyncKHR, PFNEGLCREATESYNCKHRPROC);
  get_proc(eglDestroySyncKHR, PFNEGLDESTROYSYNCKHRPROC);
  get_proc(eglWaitSyncKHR, PFNEGLWAITSYNCKHRPROC);
  get_proc(eglClientWaitSyncKHR, PFNEGLCLIENTWAITSYNCKHRPROC);
  get_proc(eglDupNativeFenceFDANDROID, PFNEGLDUPNATIVEFENCEFDANDROIDPROC);

  printf("Using display %p with EGL version %d.%d\n", gl_.display, major, minor);

  printf("EGL Version \"%s\"\n", eglQueryString(gl_.display, EGL_VERSION));
  printf("EGL Vendor \"%s\"\n", eglQueryString(gl_.display, EGL_VENDOR));
  printf("EGL Extensions \"%s\"\n", eglQueryString(gl_.display, EGL_EXTENSIONS));
  
  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    printf("failed to bind api EGL_OPENGL_ES_API\n");
    return -1;
  }
  if (!eglChooseConfig(gl_.display, config_attribs, &gl_.config, 1, &n) ||
      n != 1) {
    printf("failed to choose config: %d\n", n);
    return -1;
  }
  gl_.context =
      eglCreateContext(gl_.display, gl_.config, EGL_NO_CONTEXT, context_attribs);
  if (gl_.context == NULL) {
    printf("failed to create context\n");
    return -1;
  }
  eglMakeCurrent(gl_.display, EGL_NO_SURFACE, EGL_NO_SURFACE, gl_.context);
  
  int gbm_bo_fd = native_handle_.import_data.fd;
  const EGLint image_attrs[] = {
        EGL_WIDTH,                     width,
        EGL_HEIGHT,                    height,
        EGL_LINUX_DRM_FOURCC_EXT,      DRM_FORMAT_XRGB8888,
        EGL_DMA_BUF_PLANE0_FD_EXT,     gbm_bo_fd,
        EGL_DMA_BUF_PLANE0_PITCH_EXT,  gbm_bo_get_stride(gbm_bo_),
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_NONE,
    };

  egl_image_ =
      gl_.eglCreateImageKHR(gl_.display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                             (EGLClientBuffer)NULL, image_attrs);
  if (!egl_image_) {
     printf("failed to create EGLImage from gbm_bo\n");
     return false;
  }

  glGenRenderbuffers(1, &gl_renderbuffer_);
  glBindRenderbuffer(GL_RENDERBUFFER, gl_renderbuffer_);
  gl_.glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, egl_image_);
  if (glGetError() != GL_NO_ERROR) {
    printf("failed to create GL renderbuffer from EGLImage\n");
    return false;
  }

  glGenFramebuffers(1, &gl_framebuffer_);
  glBindFramebuffer(GL_FRAMEBUFFER, gl_framebuffer_);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_RENDERBUFFER, gl_renderbuffer_);
  if (glGetError() != GL_NO_ERROR) {
    printf("failed to create GL framebuffer\n");
    return false;
  }

  return true;
}

void GLLayerRenderer::Draw(int64_t * pfence)
{
  eglMakeCurrent(gl_.display, EGL_NO_SURFACE, EGL_NO_SURFACE, gl_.context);
  
  glBindFramebuffer(GL_FRAMEBUFFER, gl_framebuffer_);
  //glClearColor(0.5, 0.5, 0.5, 1.0);
  //glClear(GL_COLOR_BUFFER_BIT);
  //glFinish();

  glDrawFrame();
  
  //EGLSyncKHR gpu_fence = create_fence(EGL_NO_NATIVE_FENCE_FD_ANDROID);
  EGLint attrib_list[] = {
      EGL_SYNC_NATIVE_FENCE_FD_ANDROID, EGL_NO_NATIVE_FENCE_FD_ANDROID, EGL_NONE,
  };
  EGLSyncKHR gpu_fence = gl_.eglCreateSyncKHR(
      gl_.display, EGL_SYNC_NATIVE_FENCE_ANDROID, attrib_list);
  assert(gpu_fence);

  int64_t gpu_fence_fd = gl_.eglDupNativeFenceFDANDROID(gl_.display, gpu_fence);
  gl_.eglDestroySyncKHR(gl_.display, gpu_fence);
  assert(gpu_fence_fd != -1);
  *pfence = gpu_fence_fd;
}
