#include "render/render_frame.h"

#include <glad/glad.h>

#include "app/state/overlay_state.h"
#include "app/state/render_runtime_state.h"
#include "interaction/camera.h"
#include "particle_visual_config.h"
#include "render/frame_matrices.h"
#include "render/render_draw_helpers.h"
#include "render/render_system.h"

static void DrawSceneObjectsPass(const RenderRuntimeState& render,
                                 RenderSystem& rs,
                                 const FrameMatrices& fm)
{
  RenderDrawContext ctx;
  ctx.model            = fm.model;
  ctx.view             = fm.view;
  ctx.projection       = fm.projection;
  ctx.solidProgram     = rs.programs.instancedSolid;
  ctx.wireProgram      = rs.programs.line;
  ctx.lineProgram      = rs.programs.line;
  ctx.coordProgram     = rs.programs.coord;
  ctx.colorbarProgram  = rs.programs.colorbar;
#ifdef ISO_CONTOUR
  ctx.isoContourProgram = rs.programs.isocontour;
#endif

  SyncAndDraw(rs.ellipsoid,
              rs.resources.ellipsoidRenderData,
              rs.resources.ellipsoidsGpuDirty,
              ctx,
              render.ellipsoids);

  SyncAndDraw(rs.disk,
              rs.resources.diskRenderData,
              rs.resources.disksGpuDirty,
              ctx,
              render.disks);

  SyncAndDraw(rs.cube,
              rs.resources.cubeRenderData,
              rs.resources.cubesGpuDirty,
              ctx,
              render.cubes);

  SyncAndDraw(rs.cuboid,
              rs.resources.cuboidRenderData,
              rs.resources.cuboidsGpuDirty,
              ctx,
              render.cuboids);

  SyncAndDraw(rs.line,
              rs.resources.lineRenderData,
              rs.resources.linesGpuDirty,
              ctx,
              render.lines);

#ifdef USE_CONVEX_HULL
  SyncAndDraw(rs.polyhedron,
              rs.resources.polyhedronRenderData,
              rs.resources.polyhedraGpuDirty,
              ctx,
              render.polyhedra);
#endif

#ifdef ISO_CONTOUR
  SyncAndDraw(rs.isocontour,
              rs.resources.isoContourRenderData,
              rs.resources.isoContourGpuDirty,
              ctx,
              render.isocontour);
#endif
}

static ColorBarLabelLayout ComputeColorbarLayout(const RenderViewport& viewport,
                                                 const ColorbarLayoutSettings& settings)
{
  ColorBarLabelLayout layout;

  const float width  = static_cast<float>(viewport.width);
  const float height = static_cast<float>(viewport.height);

  layout.left_pixel   = width  - settings.width  - settings.margin;
  layout.right_pixel  = width  - settings.margin;
  layout.bottom_pixel = height - settings.margin;
  layout.top_pixel    = height - settings.height - settings.margin;

  layout.offsetX = static_cast<float>(viewport.x);
  layout.offsetY = static_cast<float>(viewport.y);

  return layout;
}

static ColorbarGizmoState BuildColorbarGizmoState(const RenderRuntimeState& render,
                                                  const ParticleVisualConfig& particleVisual,
                                                  const RenderViewport& viewport)
{
  ColorbarGizmoState state;
  state.visible = render.colorbar.show;

  const int ptype = render.colorbar.sourceParticleType;
  const auto& vis = particleVisual.types[ptype];

  state.content.colormapIndex = vis.colormapIndex;
  state.content.valueMin      = vis.colorMin;
  state.content.valueMax      = vis.colorMax;
  state.content.numTicks      = render.colorbar.numTicks;

  state.effectiveWidth  = static_cast<float>(viewport.width);
  state.effectiveHeight = static_cast<float>(viewport.height);
  state.layout = ComputeColorbarLayout(viewport, render.colorbar.layout);

  return state;
}

static CrossGizmoState BuildCrossGizmoState(const CameraContext& camera,
                                            const CrossGizmoRenderState& gizmo)
{
  CrossGizmoState state;
  state.visible      = gizmo.show;
  state.cameraPos    = camera.cameraPos;
  state.cameraTarget = camera.cameraTarget;
  state.cameraUp     = camera.cameraUp;
  state.crossSize    = gizmo.size;
  return state;
}

static CoordAxesGizmoState BuildCoordAxesGizmoState(const CoordAxesRenderState& axes)
{
  CoordAxesGizmoState state;
  state.visible = axes.show;
  return state;
}

static void DrawOverlayPass(const OverlayState& overlay,
                            const RenderRuntimeState& render,
                            const ParticleVisualConfig& particleVisual,
                            const CameraContext& camera,
                            RenderSystem& rs,
                            const RenderViewport& viewport,
                            const FrameMatrices& fm)
{
  overlay.particleLabels.draw(fm.view, fm.projection, viewport);

  GizmoDrawContext gctx;
  gctx.view            = fm.view;
  gctx.projection      = fm.projection;
  gctx.lineProgram     = rs.programs.line;
  gctx.coordProgram    = rs.programs.coord;
  gctx.colorbarProgram = rs.programs.colorbar;

  const CrossGizmoState crossState =
      BuildCrossGizmoState(camera, render.crossGizmo);
  const CoordAxesGizmoState axesState =
      BuildCoordAxesGizmoState(render.coordAxes);
  const ColorbarGizmoState colorbarState =
      BuildColorbarGizmoState(render, particleVisual, viewport);

  rs.crossGizmo.draw(gctx, crossState);
  rs.coordAxes.draw(gctx, axesState);
  rs.colorbar.draw(gctx, colorbarState);
  rs.colorbarLabels.draw(colorbarState);
}

static void DrawParticlePass(const ParticleVisualConfig& particleVisual,
                             const RenderRuntimeState& render,
                             RenderSystem& rs,
                             const FrameMatrices& fm)
{
  if (rs.resources.particlesGpuDirty) {
    rs.particle.sync(rs.resources.renderParticles);
    rs.resources.particlesGpuDirty = false;
  }

  rs.particle.draw(rs.programs.particle,
                   fm.model,
                   fm.view,
                   fm.projection,
                   particleVisual,
                   rs.colorbar);

  if (render.velocity.show) {
    if (rs.resources.velocityGpuDirty) {
      rs.velocity.sync(rs.resources.velocityInstanceData);
      rs.resources.velocityGpuDirty = false;
    }

    rs.velocity.draw(rs.programs.velocityArrow,
                     fm.view,
                     fm.projection,
                     render.velocity.arrowScale,
                     render.velocity.useLogScale);
  }
}

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
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!rs.frame.valid) {
    return;
  }

  const FrameMatrices& fm = rs.frame.matrices;
  const RenderViewport& viewport = rs.frame.viewport;
  const CameraContext& camera = rs.frame.camera;
  const ParticleVisualConfig& particleVisual = rs.frame.particleVisual;
  const RenderRuntimeState& render = rs.frame.runtime;
  const OverlayState& overlay = rs.frame.overlay;

  DrawParticlePass(particleVisual,
                   render,
                   rs,
                   fm);

  DrawSceneObjectsPass(render,
                       rs,
                       fm);

  DrawOverlayPass(overlay,
                  render,
                  particleVisual,
                  camera,
                  rs,
                  viewport,
                  fm);
}
