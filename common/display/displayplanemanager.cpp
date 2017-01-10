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

#include "displayplanemanager.h"

#include <set>
#include <utility>

#include <drm/drm_fourcc.h>

#include <overlaylayer.h>
#include <nativebufferhandler.h>

#include "displayplane.h"
#include "hwctrace.h"
#include "overlaybuffer.h"

namespace hwcomposer {

DisplayPlaneManager::DisplayPlaneManager(int gpu_fd, uint32_t pipe_id,
                                         uint32_t crtc_id)
    : crtc_id_(crtc_id), pipe_(pipe_id), gpu_fd_(gpu_fd) {
}

DisplayPlaneManager::~DisplayPlaneManager() {
}

bool DisplayPlaneManager::Initialize() {
  ScopedDrmPlaneResPtr plane_resources(drmModeGetPlaneResources(gpu_fd_));
  if (!plane_resources) {
    ETRACE("Failed to get plane resources");
    return false;
  }

  uint32_t num_planes = plane_resources->count_planes;
  uint32_t pipe_bit = 1 << pipe_;
  std::set<uint32_t> plane_ids;
  for (uint32_t i = 0; i < num_planes; ++i) {
    ScopedDrmPlanePtr drm_plane(
        drmModeGetPlane(gpu_fd_, plane_resources->planes[i]));
    if (!drm_plane) {
      ETRACE("Failed to get plane ");
      return false;
    }

    if (!(pipe_bit & drm_plane->possible_crtcs))
      continue;

    uint32_t formats_size = drm_plane->count_formats;
    plane_ids.insert(drm_plane->plane_id);
    std::unique_ptr<DisplayPlane> plane(
        CreatePlane(drm_plane->plane_id, drm_plane->possible_crtcs));
    std::vector<uint32_t> supported_formats(formats_size);
    for (uint32_t j = 0; j < formats_size; j++)
      supported_formats[j] = drm_plane->formats[j];

    if (plane->Initialize(gpu_fd_, supported_formats)) {
      if (plane->type() == DRM_PLANE_TYPE_CURSOR) {
        cursor_planes_.emplace_back(std::move(plane));
      } else if (plane->type() == DRM_PLANE_TYPE_PRIMARY) {
        plane->SetEnabled(true);
        primary_planes_.emplace_back(std::move(plane));
      } else if (plane->type() == DRM_PLANE_TYPE_OVERLAY) {
        overlay_planes_.emplace_back(std::move(plane));
      }
    }
  }

  if (!primary_planes_.size()) {
    ETRACE("Failed to get primary plane for display %d", crtc_id_);
    return false;
  }

  std::sort(
      cursor_planes_.begin(), cursor_planes_.end(),
      [](const std::unique_ptr<DisplayPlane> &l,
         const std::unique_ptr<DisplayPlane> &r) { return l->id() < r->id(); });

  std::sort(
      primary_planes_.begin(), primary_planes_.end(),
      [](const std::unique_ptr<DisplayPlane> &l,
         const std::unique_ptr<DisplayPlane> &r) { return l->id() < r->id(); });

  std::sort(
      overlay_planes_.begin(), overlay_planes_.end(),
      [](const std::unique_ptr<DisplayPlane> &l,
         const std::unique_ptr<DisplayPlane> &r) { return l->id() < r->id(); });

  return true;
}

bool DisplayPlaneManager::BeginFrameUpdate(
    std::vector<OverlayLayer> &layers, NativeBufferHandler *buffer_handler) {
  for (auto i = cursor_planes_.begin(); i != cursor_planes_.end(); ++i) {
    (*i)->SetEnabled(false);
  }

  for (auto i = overlay_planes_.begin(); i != overlay_planes_.end(); ++i) {
    (*i)->SetEnabled(false);
  }

  for (auto i = overlay_buffers_.begin(); i != overlay_buffers_.end(); ++i) {
    (*i)->SetInUse(false);
  }

  size_t size = layers.size();
  for (size_t layer_index = 0; layer_index < size; layer_index++) {
    std::unique_ptr<OverlayBuffer> plane_buffer;
    OverlayLayer *layer = &layers.at(layer_index);
    HwcBuffer bo;
    if (!buffer_handler->ImportBuffer(layer->GetNativeHandle(), &bo)) {
      ETRACE("Failed to Import buffer.");
      return false;
    }

    OverlayBuffer *buffer = NULL;
    buffer = GetOverlayBuffer(bo);

    if (!buffer) {
      plane_buffer.reset(new OverlayBuffer());
      overlay_buffers_.emplace_back(std::move(plane_buffer));
      buffer = overlay_buffers_.back().get();
    }

    buffer->Initialize(bo);
    layer->SetBuffer(buffer);
    IDISPLAYMANAGERTRACE("Buffer set for layer %d:", layer->GetIndex());
  }

  return true;
}

std::tuple<bool, DisplayPlaneStateList> DisplayPlaneManager::ValidateLayers(
    std::vector<OverlayLayer> &layers) {
  CTRACE();
  DisplayPlaneStateList composition;
  std::vector<OverlayPlane> commit_planes;
  OverlayLayer *cursor_layer = NULL;
  auto layer_begin = layers.begin();
  auto layer_end = layers.end();
  bool render_layers = false;
  IDISPLAYMANAGERTRACE("ValidateLayers: Total Layers:%d", layers.size());
  // We start off with Primary plane.
  DisplayPlane *current_plane = primary_planes_.begin()->get();

  OverlayLayer *primary_layer = &(*(layers.begin()));
  commit_planes.emplace_back(OverlayPlane(current_plane, primary_layer));
  composition.emplace_back(current_plane, primary_layer,
                           primary_layer->GetIndex());
  ++layer_begin;

  // Lets ensure we fall back to GPU composition in case
  // primary layer cannot be scanned out directly.
  if (FallbacktoGPU(current_plane, primary_layer, commit_planes)) {
    DisplayPlaneState &last_plane = composition.back();
    render_layers = true;
    // Case where we have just one layer which needs to be composited using
    // GPU.
    last_plane.ForceGPURendering();

    for (auto i = layer_begin; i != layer_end; ++i) {
      last_plane.AddLayer(i->GetIndex());
    }
    DUMPTRACE("All layers rendered using GPU along with primary.");
    // We need to composite primary using GPU, lets use this for
    // all layers in this case.
    return std::make_tuple(render_layers, std::move(composition));
  }

  // We are just compositing Primary layer and nothing else.
  if (layers.size() == 1) {
    IDISPLAYMANAGERTRACE("Scanning only primary for the frame.");
    return std::make_tuple(render_layers, std::move(composition));
  }

  // Retrieve cursor layer data and delete it from the layers.
  for (auto j = layers.rbegin(); j != layers.rend(); ++j) {
    if (j->GetBuffer()->GetUsage() & kLayerCursor) {
      cursor_layer = &(*(j));
      layer_end = std::next(j).base();
      break;
    }
  }
  IDISPLAYMANAGERTRACE("Total Overlay Layers: %d",
                       cursor_layer ? layers.size() - 2 : layers.size() - 1);
  if (layer_begin != layer_end) {
    // Handle layers for overlay
    for (auto j = overlay_planes_.begin(); j != overlay_planes_.end(); ++j) {
      commit_planes.emplace_back(OverlayPlane(j->get(), NULL));
      DisplayPlaneState &last_plane = composition.back();
      // Handle remaining overlay planes.
      for (auto i = layer_begin; i != layer_end; ++i, ++layer_begin) {
        OverlayLayer *layer = &(*(i));
        commit_planes.back().layer = layer;
        // If we are able to composite buffer with the given plane, lets use
        // it.
	if (!FallbacktoGPU(j->get(), layer, commit_planes)) {
          IDISPLAYMANAGERTRACE("Overlay Layer marked for scanout: %d",
                               i->GetIndex());
          composition.emplace_back(j->get(), layer, i->GetIndex());
          ++layer_begin;
          break;
        } else {
	  IDISPLAYMANAGERTRACE("Overlay Layer cannot be scanned directly: %d",
                               i->GetIndex());
          last_plane.AddLayer(i->GetIndex());
        }
      }

      if (last_plane.GetCompositionState() == DisplayPlaneState::State::kRender)
	render_layers = true;
    }

    DisplayPlaneState &last_plane = composition.back();
    // We dont have any additional planes. Pre composite remaining layers
    // to the last overlay plane.
    for (auto i = layer_begin; i != layer_end; ++i) {
      IDISPLAYMANAGERTRACE(
	  "More layers than overlay planes. Layer being added to render list: %d",
          i->GetIndex());
      last_plane.AddLayer(i->GetIndex());
    }

    if (last_plane.GetCompositionState() == DisplayPlaneState::State::kRender)
      render_layers = true;
  }

  // Handle Cursor layer.
  DisplayPlane *cursor_plane = NULL;
  if (cursor_layer) {
    IDISPLAYMANAGERTRACE("Cursor layer present. Index: %d",
                         cursor_layer->GetIndex());
    // Handle Cursor layer. If we have dedicated cursor plane, try using it
    // to composite cursor layer.
    cursor_plane =
        cursor_planes_.empty() ? NULL : cursor_planes_.begin()->get();
    if (cursor_plane) {
      commit_planes.emplace_back(OverlayPlane(cursor_plane, cursor_layer));
      // Lets ensure we fall back to GPU composition in case
      // cursor layer cannot be scanned out directly.
      if (FallbacktoGPU(cursor_plane, cursor_layer, commit_planes)) {
        cursor_plane = NULL;
      }
    }

    // We need to do this here to avoid compositing cursor with any previous
    // pre-composited planes.
    if (cursor_plane) {
      IDISPLAYMANAGERTRACE("Cursor layer marked for scan out. Index: %d",
                           cursor_layer->GetIndex());
      composition.emplace_back(cursor_plane, cursor_layer,
                               cursor_layer->GetIndex());
    } else {
      IDISPLAYMANAGERTRACE("Cursor layer added to pre-comp list. Index: %d",
                           cursor_layer->GetIndex());
      DisplayPlaneState &last_plane = composition.back();
      render_layers = true;
      last_plane.AddLayer(cursor_layer->GetIndex());
    }
  }
  IDISPLAYMANAGERTRACE("ValidateLayers Ends----------------");
  return std::make_tuple(render_layers, std::move(composition));
}

bool DisplayPlaneManager::CommitFrame(DisplayPlaneStateList &comp_planes,
                                      drmModeAtomicReqPtr pset,
                                      bool needs_modeset,
                                      PageFlipState *state) {
  CTRACE();
  if (!pset) {
    ETRACE("Failed to allocate property set %d", -ENOMEM);
    return false;
  }

  uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT;
  if (needs_modeset) {
    flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
  } else {
#ifdef DISABLE_OVERLAY_USAGE
    flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
#else
    flags |= DRM_MODE_ATOMIC_NONBLOCK;
#endif
  }

  for (DisplayPlaneState &comp_plane : comp_planes) {
    DisplayPlane *plane = comp_plane.plane();
    OverlayLayer *layer = comp_plane.GetOverlayLayer();
    IDISPLAYMANAGERTRACE("Adding layer for Display Composition. Index: %d",
                         layer->GetIndex());

    if (!plane->UpdateProperties(pset, crtc_id_, layer))
      return false;

    plane->SetEnabled(true);
    layer->GetBuffer()->SetInUse(true);
  }

  // Disable unused planes.
  for (auto i = cursor_planes_.begin(); i != cursor_planes_.end(); ++i) {
    if ((*i)->IsEnabled())
      continue;

    (*i)->Disable(pset);
  }

  for (auto i = overlay_planes_.begin(); i != overlay_planes_.end(); ++i) {
    if ((*i)->IsEnabled())
      continue;

    (*i)->Disable(pset);
  }

  int ret = drmModeAtomicCommit(gpu_fd_, pset, flags, state);

  if (ret) {
    // if (ret == -EBUSY)
    //  ret = drmModeAtomicCommit(gpu_fd_, pset, 0, state);

    if (ret) {
      ETRACE("Failed to commit pset ret=%s\n", PRINTERROR());
      return false;
    }
  }

  return true;
}

bool DisplayPlaneManager::TestCommit(
    const std::vector<OverlayPlane> &commit_planes) const {
  ScopedDrmAtomicReqPtr pset(drmModeAtomicAlloc());
  IDISPLAYMANAGERTRACE("Total planes for Test Commit. %d ",
                       commit_planes.size());
  for (auto i = commit_planes.begin(); i != commit_planes.end(); i++) {
    if (!(i->plane->UpdateProperties(pset.get(), crtc_id_, i->layer))) {
      IDISPLAYMANAGERTRACE("Failed to update Plane. %s ", PRINTERROR());
      return false;
    }
  }

  if (drmModeAtomicCommit(gpu_fd_, pset.get(), DRM_MODE_ATOMIC_TEST_ONLY,
                          NULL)) {
    IDISPLAYMANAGERTRACE("Test Commit Failed. %s ", PRINTERROR());
    return false;
  }

  return true;
}

void DisplayPlaneManager::EndFrameUpdate() {
  for (auto i = overlay_buffers_.begin(); i != overlay_buffers_.end();) {
    OverlayBuffer *buffer = i->get();
    if (buffer->InUse()) {
      buffer->IncrementRefCount();
      i++;
      continue;
    }

    buffer->DecreaseRefCount();

    if (buffer->RefCount() >= 0) {
      i++;
      continue;
    }
    IDISPLAYMANAGERTRACE("Deleted Buffer.");
    i->reset(nullptr);
    i = overlay_buffers_.erase(i);
  }
}

bool DisplayPlaneManager::FallbacktoGPU(
    DisplayPlane *target_plane, OverlayLayer *layer,
    const std::vector<OverlayPlane> &commit_planes) const {
  IDISPLAYMANAGERTRACE("FallbacktoGPU Called for layer: %d",
                       layer->GetIndex());
  if (!target_plane->ValidateLayer(layer))
    return true;

  if (layer->GetBuffer()->GetFb() == 0) {
    if (!layer->GetBuffer()->CreateFrameBuffer(gpu_fd_)) {
      DUMPTRACE("Failed to create frame buffer for layer %d",
                layer->GetIndex());
    }

    return true;
  }

  // TODO(kalyank): Take relevant factors into consideration to determine if
  // Plane Composition makes sense. i.e. layer size etc

  if (!TestCommit(commit_planes)) {
    IDISPLAYMANAGERTRACE("TestCommit failed.");
    return true;
  }

  return false;
}

std::unique_ptr<DisplayPlane> DisplayPlaneManager::CreatePlane(
    uint32_t plane_id, uint32_t possible_crtcs) {
  return std::unique_ptr<DisplayPlane>(
      new DisplayPlane(plane_id, possible_crtcs));
}

OverlayBuffer *DisplayPlaneManager::GetOverlayBuffer(const HwcBuffer &bo) {
  if (overlay_buffers_.empty())
    return NULL;

  for (auto i = overlay_buffers_.begin(); i != overlay_buffers_.end(); ++i) {
    OverlayBuffer *buffer = i->get();
    if (buffer->IsCompatible(bo)) {
      buffer->IncrementRefCount();
      return buffer;
    }
  }

  return NULL;
}

}  // namespace hwcomposer
