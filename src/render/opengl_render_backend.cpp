#include "render/opengl_render_backend.h"

#include <glad/glad.h>

#include <vector>

#include "app/state/overlay_state.h"
#include "app/state/render_runtime_state.h"
#include "interaction/camera.h"
#include "particle_visual_config.h"
#include "projection/projection_map_ui_state.h"
#include "render/colormap_defs.h"
#include "render/frame_matrices.h"
#include "render/render_draw_helpers.h"
#include "render/render_resources.h"
#include "render/render_system.h"
#include "render/render_viewport.h"

static ColorBarLabelLayout ComputeColorbarLayout(
  const RenderViewport& viewport,
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

static ColorbarGizmoState BuildColorbarGizmoState(
  const RenderRuntimeState& render,
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

static CrossGizmoState BuildCrossGizmoState(
  const CameraContext& camera,
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

static CoordAxesGizmoState BuildCoordAxesGizmoState(
  const CoordAxesRenderState& axes)
{
  CoordAxesGizmoState state;
  state.visible = axes.show;
  return state;
}

void OpenGLRenderBackend::init()
{
  InitRenderPrograms(programs_);

  particle_.init();
  velocity_.init();

  ellipsoid_.init();
  disk_.init();
  line_.init();
  cube_.init();
  cuboid_.init();
  polyhedron_.clearGpuCache();

#ifdef ISO_CONTOUR
  // isocontour_.init(); // Needed only when the renderer owns GL objects.
#endif

  crossGizmo_.init();
  coordAxes_.init();

  std::vector<ColormapDefView> cmapViews;
  cmapViews.reserve(gNumColormaps);
  for (int i = 0; i < gNumColormaps; ++i) {
    cmapViews.push_back({gColormapDefs[i].data, gColormapDefs[i].count});
  }

  colorbar_.init();
  colorbar_.initColorMaps(cmapViews.data(),
                          static_cast<int>(cmapViews.size()));
}

void OpenGLRenderBackend::destroy()
{
  crossGizmo_.destroy();
  coordAxes_.destroy();
  velocity_.destroy();
  particle_.destroy();

  ellipsoid_.destroy();
  disk_.destroy();
  line_.destroy();
  cube_.destroy();
  cuboid_.destroy();
  colorbar_.destroy();
  polyhedron_.clearGpuCache();
  preview_.destroy();
  DestroyRenderPrograms(programs_);

#ifdef ISO_CONTOUR
  isocontour_.destroy();
#endif
}

void OpenGLRenderBackend::updateProjectionPreview(const RgbImage& image)
{
  preview_.update(image);
}

ProjectionPreviewUIState OpenGLRenderBackend::makeProjectionPreviewUIState() const
{
  return preview_.makeUIState();
}

void OpenGLRenderBackend::render(const RenderFrameState& frame,
                                 const RenderSceneData& scene)
{
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!frame.valid) {
    return;
  }

  glViewport(frame.viewport.x,
             frame.viewport.y,
             frame.viewport.width,
             frame.viewport.height);

  const FrameMatrices& fm = frame.matrices;
  const RenderViewport& viewport = frame.viewport;
  const CameraContext& camera = frame.camera;
  const ParticleVisualConfig& particleVisual = frame.particleVisual;
  const RenderRuntimeState& render = frame.runtime;
  const OverlayState& overlay = frame.overlay;

  SyncIfVersionChanged(particle_,
                       scene.particles,
                       scene.particlesVersion,
                       uploaded_.particles);

  particle_.draw(programs_.particle,
                 fm.model,
                 fm.view,
                 fm.projection,
                 particleVisual,
                 colorbar_);

  if (render.velocity.show) {
    SyncIfVersionChanged(velocity_,
                         scene.velocityInstances,
                         scene.velocityVersion,
                         uploaded_.velocity);

    velocity_.draw(programs_.velocityArrow,
                   fm.view,
                   fm.projection,
                   render.velocity.arrowScale,
                   render.velocity.useLogScale);
  }

  RenderDrawContext ctx;
  ctx.model            = fm.model;
  ctx.view             = fm.view;
  ctx.projection       = fm.projection;
  ctx.solidProgram     = programs_.instancedSolid;
  ctx.wireProgram      = programs_.line;
  ctx.lineProgram      = programs_.line;
  ctx.coordProgram     = programs_.coord;
  ctx.colorbarProgram  = programs_.colorbar;
#ifdef ISO_CONTOUR
  ctx.isoContourProgram = programs_.isocontour;
#endif

  SyncAndDraw(ellipsoid_,
              scene.ellipsoids,
              scene.ellipsoidsVersion,
              uploaded_.ellipsoids,
              ctx,
              render.ellipsoids);

  SyncAndDraw(disk_,
              scene.disks,
              scene.disksVersion,
              uploaded_.disks,
              ctx,
              render.disks);

  SyncAndDraw(cube_,
              scene.cubes,
              scene.cubesVersion,
              uploaded_.cubes,
              ctx,
              render.cubes);

  SyncAndDraw(cuboid_,
              scene.cuboids,
              scene.cuboidsVersion,
              uploaded_.cuboids,
              ctx,
              render.cuboids);

  SyncAndDraw(line_,
              scene.lines,
              scene.linesVersion,
              uploaded_.lines,
              ctx,
              render.lines);

#ifdef USE_CONVEX_HULL
  SyncAndDraw(polyhedron_,
              scene.polyhedra,
              scene.polyhedraVersion,
              uploaded_.polyhedra,
              ctx,
              render.polyhedra);
#endif

#ifdef ISO_CONTOUR
  SyncAndDraw(isocontour_,
              scene.isoContour,
              scene.isoContourVersion,
              uploaded_.isoContour,
              ctx,
              render.isocontour);
#endif

  overlay.particleLabels.draw(fm.view, fm.projection, viewport);

  GizmoDrawContext gctx;
  gctx.view            = fm.view;
  gctx.projection      = fm.projection;
  gctx.lineProgram     = programs_.line;
  gctx.coordProgram    = programs_.coord;
  gctx.colorbarProgram = programs_.colorbar;

  const CrossGizmoState crossState =
    BuildCrossGizmoState(camera, render.crossGizmo);
  const CoordAxesGizmoState axesState =
    BuildCoordAxesGizmoState(render.coordAxes);
  const ColorbarGizmoState colorbarState =
    BuildColorbarGizmoState(render, particleVisual, viewport);

  crossGizmo_.draw(gctx, crossState);
  coordAxes_.draw(gctx, axesState);
  colorbar_.draw(gctx, colorbarState);
  colorbarLabels_.draw(colorbarState);
}

std::unique_ptr<RenderBackend> CreateOpenGLRenderBackend()
{
  return std::make_unique<OpenGLRenderBackend>();
}
