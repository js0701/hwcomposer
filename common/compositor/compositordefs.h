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

#ifndef COMPOSITOR_DEFS_H__
#define COMPOSITOR_DEFS_H__

#include <stdint.h>

#ifdef USE_GL
#define EGL_EGLEXT_PROTOTYPES

#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

namespace hwcomposer {

// clang-format off
// Column-major order:
// float mat[4] = { 1, 2, 3, 4 } ===
// [ 1 3 ]
// [ 2 4 ]
static float TransformMatrices[] = {
    1.0f, 0.0f, 0.0f, 1.0f,  // identity matrix
    0.0f, 1.0f, 1.0f, 0.0f,  // swap x and y
};
// clang-format on

typedef unsigned GpuResourceHandle;
// Add Vulkan defs here.

#ifdef USE_GL
typedef EGLImageKHR GpuImage;
typedef EGLDisplay GpuDisplay;
#else
typedef void* GpuImage;
typedef void* GpuDisplay;
#endif

}  // namespace hwcomposer
#endif  // COMPOSITOR_DEFS_H__
