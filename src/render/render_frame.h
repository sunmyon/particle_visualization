#pragma once

#include "app/overlay_state.h"
#include "app/render_runtime_state.h"
#include "interaction/camera.h"
#include "particle_visual_config.h"

class WindowContext;
struct RenderSystem;

struct RenderFrameInput {
  CameraContext camera;
  ParticleVisualConfig particleVisual;
  RenderRuntimeState render;
  OverlayState overlay;
};

void UpdateRenderFrameInput(RenderFrameInput& input,
                            const CameraContext& camera,
                            const ParticleVisualConfig& particleVisual,
                            const RenderRuntimeState& render,
                            const OverlayState& overlay);

void PrepareRenderFrame(const CameraContext& camera,
                        const ParticleVisualConfig& particleVisual,
                        const RenderRuntimeState& render,
                        const OverlayState& overlay,
                        RenderSystem& rs,
                        const WindowContext& window);

void PrepareRenderFrame(const RenderFrameInput& input,
                        RenderSystem& rs,
                        const WindowContext& window);

void RenderScene(RenderSystem& rs,
                 const WindowContext& window);
