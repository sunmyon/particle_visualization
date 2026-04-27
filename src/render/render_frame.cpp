#include "render/render_frame.h"

#include "app/state/overlay_state.h"
#include "app/state/render_runtime_state.h"
#include "interaction/camera.h"
#include "particle_visual_config.h"
#include "render/frame_matrices.h"
#include "render/render_system.h"

void UpdateRenderFrameInput(RenderFrameInput& input,
                            const RenderViewport& viewport,
                            const CameraContext& camera,
                            const ParticleVisualConfig& particleVisual,
                            const RenderRuntimeState& render,
                            const OverlayState& overlay)
{
  input.viewport = viewport;
  input.camera = camera;
  input.particleVisual = particleVisual;
  input.render = render;
  input.overlay = overlay;
}

void PrepareRenderFrame(const CameraContext& camera,
                        const RenderViewport& viewport,
                        const ParticleVisualConfig& particleVisual,
                        const RenderRuntimeState& render,
                        const OverlayState& overlay,
                        RenderSystem& rs)
{
  rs.frame.viewport = viewport;
  rs.frame.matrices = BuildFrameMatrices(camera, viewport);
  rs.frame.camera = camera;
  rs.frame.particleVisual = particleVisual;
  rs.frame.runtime = render;
  rs.frame.overlay = overlay;
  rs.frame.valid = true;
}

void PrepareRenderFrame(const RenderFrameInput& input,
                        RenderSystem& rs)
{
  PrepareRenderFrame(input.camera,
                     input.viewport,
                     input.particleVisual,
                     input.render,
                     input.overlay,
                     rs);
}

void RenderScene(RenderSystem& rs)
{
  if (!rs.backend) {
    return;
  }

  rs.backend->render(rs.frame, rs.scene);
}
