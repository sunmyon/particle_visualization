#pragma once

#include <memory>

#include "render/render_backend.h"
#include "render/render_resources.h"
#include "render/frame_matrices.h"
#include "render/render_viewport.h"

#include "app/state/overlay_state.h"
#include "app/state/render_runtime_state.h"
#include "interaction/camera.h"
#include "render/particle_visual_config.h"

struct RenderFrameState {
  RenderViewport viewport;
  FrameMatrices matrices;
  CameraContext camera;
  ParticleVisualConfig particleVisual;
  RenderRuntimeState runtime;
  OverlayState overlay;
  bool valid = false;
};

struct RenderSystem {
  RenderSceneData scene;
  RenderSceneBuildState build;
  RenderFrameState frame;
  std::unique_ptr<RenderBackend> backend;
};

void InitRenderSystem(RenderSystem& rs);
void DestroyRenderSystem(RenderSystem& rs);
