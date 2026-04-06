#include "app/app_frame.h"

#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include "app/app_state.h"
#include "app/app_callbacks.h"
#include "render/render_system.h"

#include "UI.h"
#include "render/frame_matrices.h"
#include "render/render_draw_helpers.h"
#include "render/render_resources.h"

#include "imgui_context.h"
#include "window_context.h"
#include "FindClumps/find_clumps.h"

#ifdef PYTHON_BRIDGE
#include "PythonBridge/BridgeAdapter.h"
#include "PythonBridge/PythonBridge.h"
#include "PythonBridge/ShmLayout.h"
#endif


static void UpdateUI(AppState& app)
{
  ShowTime(app.particles->particleBlock.header.time);

  SettingsUIContext settingsCtx;
  settingsCtx.P              = app.particles;
  settingsCtx.fileInfo       = app.fileInfo;
  settingsCtx.camCtx         = &app.camera;
  settingsCtx.particleVisual = &app.particleVisual;
  settingsCtx.services       = &app.services;
  settingsCtx.render         = &app.render;
  settingsCtx.scene          = &app.scene;
    
  ShowSettingsUI(settingsCtx, app.settings);
  ShowCameraSettingsUI();

  app.services.clumpFind->ShowFindClumpsUI(app.particles->particleBlock.particles,
                                           app.particles->particleBlock.header,
					   *app.fileInfo);

#ifdef CLUMP_DATA_READ
  app.services.clumpFind->ReadAndShowClumpsUI(app.particles,
                                              app.fileInfo->currentFileIndex,
					      *app.fileInfo);
  app.services.clumpFind->showClumpChainList(app.particles,
                                             app.services.projectionMap2D.get(),
					     *app.fileInfo);
#endif

  app.fileInfo->DrawFormatDialog();
#ifdef HAVE_HDF5
  app.fileInfo->ShowHDF5FieldMappingDialog();
#endif

  const bool applied = DrawMaskWindow();
  if (applied) {
    MaskConfig cfg = MakeMaskConfigFromUI();
    app.fileInfo->setMaskConfig(cfg);
  }

#ifdef VOLUME_RENDERING
  app.services.tf->showUI();
  app.services.volume.rho2sigma = app.services.tf->bakeLUT(1024);
#endif

  DrawTopParticlesUI(app.particles, app.camera);
}

static void UpdateExternalInputs(AppState& app)
{
#ifdef PYTHON_BRIDGE
  if (app.services.py.ptr) {
    std::vector<FieldId> dirty;
    app.services.py.ptr->drainEditFields(dirty);
    if (!dirty.empty()) {
      bridge::applyFromSharedToAoS(app.services.py.ptr->shared(),
                                   *app.particles,
                                   dirty);
      app.particles->particlesDirty = true;
    }
  }
#endif
}

static void UpdateOverlayState(AppState& app)
{
  if (app.settings.flagShowSinkIDs) {
    app.particleLabels.updateIfNeeded(*app.particles,
                                      app.camera,
                                      app.settings);
  }else{
    app.particleLabels.clear();
  }
}

static void PropagateDirtyFlags(const AppState& app, RenderSystem& rs)
{
  if (app.particles->particlesDirty) {
    rs.resources.particleRenderDataDirty = true;
  }

  if (app.particles->velocityDirty) {
    rs.resources.velocityInstanceDataDirty = true;
  }
}

static void UpdateFrameState(AppState& app, RenderSystem& rs)
{
  rs.resources.cuboidRenderData.clear();
  rs.resources.lineRenderData.clear();

  PropagateDirtyFlags(app, rs);

  if (rs.resources.particleRenderDataDirty) {
    BuildRenderParticles(*app.particles,
                         app.particleVisual,
                         rs.resources.renderParticles);
    rs.resources.particleRenderDataDirty = false;
    rs.resources.particlesGpuDirty = true;
    app.particles->particlesDirty = false;
  }

  if (rs.resources.velocityInstanceDataDirty) {
    UpdateVelocityRenderData(*app.particles,
                             app.render.velocity.subtraction,
                             rs.resources.velocityInstanceData);
    rs.resources.velocityInstanceDataDirty = false;
    rs.resources.velocityGpuDirty = true;
    app.particles->velocityDirty = false;
  }

  if (app.render.cubes.cpuUpdated) {
    BuildCubeRenderData(app.scene.cube, rs.resources.cubeRenderData);
    app.render.cubes.cpuUpdated = false;
    rs.resources.cubesGpuDirty = true;
  }

  if (rs.resources.ellipsoidRenderDataDirty) {
    BuildEllipsoidRenderData(app.scene.ellipsoid,
                             rs.resources.ellipsoidRenderData);
    rs.resources.ellipsoidRenderDataDirty = false;
    rs.resources.ellipsoidsGpuDirty = true;
  }

  if (rs.resources.diskRenderDataDirty) {
    BuildDiskRenderData(app.scene.disk, rs.resources.diskRenderData);
    rs.resources.diskRenderDataDirty = false;
    rs.resources.disksGpuDirty = true;
  }

  if (rs.resources.lineRenderDataDirty) {
    BuildLineRenderData(app.scene.line, rs.resources.lineRenderData);
    rs.resources.lineRenderDataDirty = false;
    rs.resources.linesGpuDirty = true;
  }

#ifdef ISO_CONTOUR
  if (app.render.isocontour.cpuUpdated) {
    BuildIsoContourRenderData(app.services.isoContour.verts,
                              app.services.isoContour.inds,
                              rs.resources.isoContourRenderData);
    app.render.isocontour.cpuUpdated = false;
    rs.resources.isoContourGpuDirty = true;
  }
#endif

#ifdef USE_CONVEX_HULL
  if (app.services.clumpFind->checkClumpComputation()) {
    if (app.services.clumpFind->checkClearCache()) {
      app.scene.polyhedron.clearGroup("convex_hull");
      rs.polyhedron.requestResetGroup("convex_hull");
      app.services.clumpFind->finishClearCache();
    }

    app.render.polyhedra.cpuUpdated = app.services.clumpFind->get_flagUpdated();

    if (app.render.polyhedra.cpuUpdated) {
      app.scene.polyhedron.clearGroup("convex_hull");

      const int nclumps = app.services.clumpFind->get_nclumps();
      for (int i = 0; i < nclumps; ++i) {
        if (!app.services.clumpFind->flagShowHull(i))
          continue;

        TrackingVector<ParticleData> pts =
          app.services.clumpFind->get_particle_indices(i,
                                                       app.particles->particleBlock.particles);

        TrackingVector<float> vertices =
          app.convexHullGenerator->buildLineVertices(pts);

        PolyhedronObject obj;
        obj.vertices.reserve(vertices.size() / 3);
        for (size_t k = 0; k + 2 < vertices.size(); k += 3) {
          obj.vertices.emplace_back(vertices[k], vertices[k + 1], vertices[k + 2]);
        }

        obj.color   = glm::vec3(1.0f);
        obj.opacity = app.render.polyhedra.opacity;
        obj.tag     = "convex_hull";

        app.scene.polyhedron.add(i, obj);
      }

      BuildPolyhedronRenderData(app.scene.polyhedron,
                                rs.resources.polyhedronRenderData);

      rs.resources.polyhedraGpuDirty = true;
      app.render.polyhedra.show = true;
      app.render.polyhedra.cpuUpdated = false;
      app.services.clumpFind->disable_flagUpdated();

      rs.polyhedron.requestResetGroup("convex_hull");
    }
  }
#endif

  if (app.render.cuboidAnnotations.cpuUpdated) {
    app.scene.cuboidAnnotation.clear();

    if (app.render.cuboidAnnotations.show) {
      CuboidAnnotationObject obj;
      obj.cuboid = app.services.projectionMap2D->interactiveCuboid();

      obj.cuboid.edgeColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
      obj.highlightColor   = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
      obj.arrowColor       = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

      const int axis = app.services.projectionMap2D->params.selectedAxis;
      if (axis == 0)      obj.selectedAxis = CuboidAxis::X;
      else if (axis == 1) obj.selectedAxis = CuboidAxis::Y;
      else                obj.selectedAxis = CuboidAxis::Z;

      obj.showAxisHighlight = true;
      obj.showArrow         = true;
      obj.arrowLength       = 0.2f;
      obj.arrowHeadLength   = 0.05f;
      obj.arrowHeadWidth    = 0.03f;
      obj.tag               = "interactive_cuboid";

      app.scene.cuboidAnnotation.add(obj);

      AppendCuboidAnnotationRenderData(app.scene.cuboidAnnotation,
                                       rs.resources.cuboidRenderData);
      rs.resources.cuboidsGpuDirty = true;
      app.render.cuboids.show = true;

      AppendCuboidArrowRenderData(app.scene.cuboidAnnotation,
                                  rs.resources.lineRenderData);
      rs.resources.linesGpuDirty = true;
      app.render.lines.show = true;
    }

    app.render.cuboidAnnotations.cpuUpdated = false;
  }
}

static void DrawSceneObjectsPass(AppState& app,
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
              app.render.ellipsoids);

  SyncAndDraw(rs.disk,
              rs.resources.diskRenderData,
              rs.resources.disksGpuDirty,
              ctx,
              app.render.disks);

  SyncAndDraw(rs.cube,
              rs.resources.cubeRenderData,
              rs.resources.cubesGpuDirty,
              ctx,
              app.render.cubes);

  SyncAndDraw(rs.cuboid,
              rs.resources.cuboidRenderData,
              rs.resources.cuboidsGpuDirty,
              ctx,
              app.render.cuboids);

  SyncAndDraw(rs.line,
              rs.resources.lineRenderData,
              rs.resources.linesGpuDirty,
              ctx,
              app.render.lines);

#ifdef USE_CONVEX_HULL
  SyncAndDraw(rs.polyhedron,
              rs.resources.polyhedronRenderData,
              rs.resources.polyhedraGpuDirty,
              ctx,
              app.render.polyhedra);
#endif

#ifdef ISO_CONTOUR
  SyncAndDraw(rs.isocontour,
              rs.resources.isoContourRenderData,
              rs.resources.isoContourGpuDirty,
              ctx,
              app.render.isocontour);
#endif
}

static ColorBarLabelLayout ComputeColorbarLayout(const WindowContext& window,
                                                 const ColorbarLayoutSettings& settings)
{
  ColorBarLabelLayout layout;

  const float width  = static_cast<float>(window.viewportWidth());
  const float height = static_cast<float>(window.viewportHeight());

  layout.left_pixel   = width  - settings.width  - settings.margin;
  layout.right_pixel  = width  - settings.margin;
  layout.bottom_pixel = height - settings.margin;
  layout.top_pixel    = height - settings.height - settings.margin;

  layout.offsetX = static_cast<float>(window.viewportX());
  layout.offsetY = static_cast<float>(window.viewportY());

  return layout;
}

static ColorbarGizmoState BuildColorbarGizmoState(const AppState& app,
                                                  const WindowContext& window)
{
  ColorbarGizmoState state;
  state.visible = app.render.colorbar.show;

  const int ptype = app.render.colorbar.sourceParticleType;
  const auto& vis = app.particleVisual.types[ptype];

  state.content.colormapIndex = vis.colormapIndex;
  state.content.valueMin      = vis.colorMin;
  state.content.valueMax      = vis.colorMax;
  state.content.numTicks      = app.render.colorbar.numTicks;

  state.effectiveWidth  = static_cast<float>(window.viewportWidth());
  state.effectiveHeight = static_cast<float>(window.viewportHeight());
  state.layout = ComputeColorbarLayout(window, app.render.colorbar.layout);

  return state;
}

static CrossGizmoState BuildCrossGizmoState(const AppState& app)
{
  CrossGizmoState state;
  state.visible      = app.render.crossGizmo.show;
  state.cameraPos    = app.camera.cameraPos;
  state.cameraTarget = app.camera.cameraTarget;
  state.cameraUp     = app.camera.cameraUp;
  state.crossSize    = app.render.crossGizmo.size;
  return state;
}

static CoordAxesGizmoState BuildCoordAxesGizmoState(const AppState& app)
{
  CoordAxesGizmoState state;
  state.visible = app.render.coordAxes.show;
  return state;
}

static void DrawOverlayPass(AppState& app,
                            RenderSystem& rs,
                            const WindowContext& window,
                            const FrameMatrices& fm)
{
  app.particleLabels.draw(fm.view,
                          fm.projection,
                          window);

  GizmoDrawContext gctx;
  gctx.view            = fm.view;
  gctx.projection      = fm.projection;
  gctx.lineProgram     = rs.programs.line;
  gctx.coordProgram    = rs.programs.coord;
  gctx.colorbarProgram = rs.programs.colorbar;

  const CrossGizmoState crossState = BuildCrossGizmoState(app);
  const CoordAxesGizmoState axesState = BuildCoordAxesGizmoState(app);
  const ColorbarGizmoState colorbarState = BuildColorbarGizmoState(app, window);

  rs.crossGizmo.draw(gctx, crossState);
  rs.coordAxes.draw(gctx, axesState);
  rs.colorbar.draw(gctx, colorbarState);
  rs.colorbarLabels.draw(colorbarState);
}

static void DrawParticlePass(AppState& app,
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
                   app.particleVisual,
                   rs.colorbar);

  if (app.render.velocity.show) {
    if (rs.resources.velocityGpuDirty) {
      rs.velocity.sync(rs.resources.velocityInstanceData);
      rs.resources.velocityGpuDirty = false;
    }

    rs.velocity.draw(rs.programs.velocityArrow,
                     fm.view,
                     fm.projection,
                     app.render.velocity.arrowScale,
                     app.render.velocity.useLogScale);
  }
}

static void RenderScene(AppState& app, RenderSystem& rs, const WindowContext& window)
{
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  FrameMatrices fm = BuildFrameMatrices(app.camera, window);

  DrawParticlePass(app, rs, fm);
  DrawSceneObjectsPass(app, rs, fm);
  DrawOverlayPass(app, rs, window, fm);

#ifdef VOLUME_RENDERING
  // DrawVolumePass(...);
#endif
}

void RunFrame(AppState& app,
	      RenderSystem& render,
	      WindowContext& window)
{
  float currentFrame = static_cast<float>(glfwGetTime());
  float deltaTime = app.interaction.beginFrame(currentFrame);
  (void)deltaTime;

  processInput(window.handle());
  glfwPollEvents();

  BeginImGuiFrame();

  UpdateUI(app);
  UpdateExternalInputs(app);
  UpdateOverlayState(app);
  UpdateFrameState(app, render);

  render.preview.update(app.services.projectionMap2D->getImage());
  ProjectionPreviewUIState previewUI = render.preview.makeUIState();
  DrawProjectionPreviewUI(*app.services.projectionMap2D, previewUI);

  RenderScene(app, render, window);

  EndImGuiFrame();
  glfwSwapBuffers(window.handle());
}

