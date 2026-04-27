#pragma once

#include "app/state/overlay_state.h"
#include "app/state/render_runtime_state.h"
#include "interaction/camera.h"
#include "particle_visual_config.h"
#include "render/render_viewport.h"

struct RenderSystem;

struct RenderFrameInput {
  RenderViewport viewport;
  CameraContext camera;
  ParticleVisualConfig particleVisual;
  RenderRuntimeState render;
  OverlayState overlay;
};

void UpdateRenderFrameInput(RenderFrameInput& input,
                            const RenderViewport& viewport,
                            const CameraContext& camera,
                            const ParticleVisualConfig& particleVisual,
                            const RenderRuntimeState& render,
                            const OverlayState& overlay);

void PrepareRenderFrame(const CameraContext& camera,
                        const RenderViewport& viewport,
                        const ParticleVisualConfig& particleVisual,
                        const RenderRuntimeState& render,
                        const OverlayState& overlay,
                        RenderSystem& rs);

void PrepareRenderFrame(const RenderFrameInput& input,
                        RenderSystem& rs);

void RenderScene(RenderSystem& rs);
