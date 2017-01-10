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

#ifndef GL_SURFACE_H_
#define GL_SURFACE_H_

#define GL_GLEXT_PROTOTYPES

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "nativesurface.h"

namespace hwcomposer {

class NativeBufferHandler;
class OverlayBuffer;

class GLSurface : public NativeSurface {
 public:
  GLSurface() = default;
  ~GLSurface() override;
  GLSurface(uint32_t width, uint32_t height);

  void MakeCurrent() override;

 private:
  bool InitializeGPUResources() override;
  GLuint tex_ = 0;
  GLuint fb_ = 0;
};

}  // namespace hwcomposer
#endif  // GL_SURFACE_H_
