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

#ifndef COMPOSITOR_H_
#define COMPOSITOR_H_

#include <platformdefines.h>

#include "compositionregion.h"
#include "displayplanestate.h"
#include "factory.h"

namespace hwcomposer {

class InternalDisplay;
class NativeBufferHandler;
class NativeGpuResource;
class NativeSync;
struct OverlayLayer;

class Compositor {
 public:
  Compositor();
  ~Compositor();

  void Init();

  Compositor(const Compositor &) = delete;

  bool BeginFrame();
  bool Draw(DisplayPlaneStateList &planes, std::vector<OverlayLayer> &layers,
            const std::vector<HwcRect<int>> &display_frame);
  bool DrawOffscreen(std::vector<OverlayLayer> &layers,
                     const std::vector<HwcRect<int>> &display_frame,
                     const std::vector<size_t> &source_layers,
                     NativeBufferHandler *buffer_handler, uint32_t width,
                     uint32_t height, HWCNativeHandle output_handle,
                     int32_t *retire_fence);
  void InsertFence(int fence);

 private:
  bool Render(std::vector<OverlayLayer> &layers, NativeSurface *surface,
              const std::vector<CompositionRegion> &comp_regions);
  void SeparateLayers(const std::vector<size_t> &dedicated_layers,
                      const std::vector<size_t> &source_layers,
                      const std::vector<HwcRect<int>> &display_frame,
                      std::vector<CompositionRegion> &comp_regions);

  InternalDisplay *display_;
  std::unique_ptr<Renderer> renderer_;
  std::unique_ptr<NativeGpuResource> gpu_resource_handler_;
};
}

#endif  // COMPOSITOR_H_
