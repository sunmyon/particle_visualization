#include "app/app_frame.h"

#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <imgui.h>

#include "app/app_state.h"
#include "app/app_analysis_execution.h"
#include "app/app_projection_execution.h"
#include "app/app_callbacks.h"
#include "app/normalization_config.h"
#include "render/render_system.h"

#include "UI.h"
#include "settingUI.h"
#include "FileIO/file_io.h"
#include "FileIO/file_format_dialog.h"

#include "render/frame_matrices.h"
#include "render/render_draw_helpers.h"
#include "render/render_resources.h"

#include "imgui_context.h"
#include "window_context.h"
#include "projection/make_2D_projection_map.h"

#include "FindClumps/find_clumps.h"
#include "FindClumps/find_clumps_ui.h"

#ifdef USE_CONVEX_HULL
#include "geometry/convex_hull_generator.h"
#endif

#ifdef PYTHON_BRIDGE
#include "PythonBridge/BridgeAdapter.h"
#include "PythonBridge/PythonBridge.h"
#include "PythonBridge/ShmLayout.h"
#endif

static SettingsUIContext MakeSettingsUIContext(const AppDataState& data,
                                               AppViewState& view,
                                               AppRuntimeState& runtime,
                                               AnalysisDerivedState& analysis,
                                               ToolWindowUIState& toolWindows)
{
  SettingsUIContext ctx;
  ctx.P              = data.particles;
  ctx.fileInfo       = data.fileInfo;
  ctx.camCtx         = &view.camera;
  ctx.particleVisual = &view.particleVisual;
  ctx.render         = &runtime.render;
  ctx.analysis       = &analysis;
  ctx.windows        = &toolWindows;
  
  return ctx;
}

static void DrawSettingsPanels(const AppDataState& data,
                               AppViewState& view,
                               AppRuntimeState& runtime,
                               AnalysisDerivedState& analysis,
			       ToolWindowUIState& toolWindows)
{
  SettingsUIContext settingsCtx =
    MakeSettingsUIContext(data, view, runtime, analysis, toolWindows);
  ShowSettingsUI(settingsCtx, runtime);
  ShowCameraSettingsUI();
}

static void DrawFileDialogPanels(FileInfo& file,
                                 ToolWindowUIState& toolWindows)
{
  DrawBinaryFormatDialog(toolWindows.fileFormatDialog,
                         file.editSource());
#ifdef HAVE_HDF5
  DrawHDF5FormatDialog(toolWindows.fileFormatDialog,
                       file.editSource());
#endif
}

static void ApplyMaskIfRequested(MaskUIState& mask, InputFilterConfig& inputFilter)
{
  const bool applied = DrawMaskWindow(mask, inputFilter.mask);
  if (!applied) {
    return;
  }
}


static void DrawMainUI(const AppDataState& data,
		       AppViewState& view,
		       AppRuntimeState& runtime,
		       AnalysisDerivedState& analysis,
		       ToolWindowUIState& toolWindows,
		       double time)
{
  ShowTime(time);
  DrawSettingsPanels(data, view, runtime, analysis, toolWindows);
}

static void DrawToolWindows(AppDataState& data,
                            CameraContext& camera,
                            AppRuntimeState& runtime,
			    ToolWindowUIState& tools,
                            AnalysisDerivedState& analysis,
                            AppServices& services)
{
  DrawTopParticlesUI(tools.topParticles, data.particles, camera, runtime.settings.tracking, runtime.settings.snapshotPostprocess);
  
  DrawFileDialogPanels(*data.fileInfo, tools);
  ApplyMaskIfRequested(tools.mask, runtime.settings.inputFilter);

  DrawRadialProfileUI(tools.radialProfile, analysis.radial, *services.radialProfile, data.particles->particleBlock, camera.cameraTarget, runtime.settings.normalization, data.particles->units);

  const auto& src = data.fileInfo->getSource();
  DrawProjectionMapUI(tools.projectionMap,
		      runtime.analysis.projectionMap,
		      *data.particles,
		      runtime.settings.normalization,
		      camera,
		      runtime.render.cuboidAnnotations,
		      src.currentFileIndex);
  
  DrawProjectionFontSelectionUI(*services.projectionMap2D, tools.projectionMap);
  
#ifdef HAVE_HDF5
  DrawHaloesUI(tools.haloes, data.haloStore, camera, runtime.settings.normalization);
#endif
  
  Histogram2DContext histCtx;
  histCtx.cameraCenter = &camera.cameraTarget;
  
  auto visibleHulls = analysis.convexHulls.visibleHulls();
  histCtx.convexHulls = &visibleHulls;

  DrawHistogram2DUI(tools.histogram2D, analysis.hist2D, *services.histogram2D, data.particles->particleBlock, histCtx);

  DrawClumpFinderUI(tools.clumpFind,
		    *services.clumpFind,
		    data.particles->particleBlock.particles,
		    data.header,
		    data.fileInfo->getSource(),
		    camera);
  
  DrawClumpListUI(tools.clumpList,
		  *services.clumpLoad,
		  data.clumpStore,
		  runtime.settings.tracking,
		  data.fileInfo->getSource().currentFileIndex,
		  data.fileInfo->getSource(),
		  camera,
		  runtime.settings.normalization);
  
  DrawClumpChainListUI(tools.clumpChain,
		       *services.clumpChain,
		       data.particles,
		       data.header,
		       services.projectionMap2D.get(),
		       runtime.analysis.projectionMap.params,
		       *data.fileInfo,
		       camera,
		       runtime.settings.normalization,
		       runtime.settings.inputFilter);
}

static void UpdateExternalInputs(AppServices& services,
                                 ParticleArray& particles)
{
#ifdef PYTHON_BRIDGE
  if (services.py.ptr) {
    std::vector<FieldId> dirty;
    services.py.ptr->drainEditFields(dirty);
    if (!dirty.empty()) {
      bridge::applyFromSharedToAoS(services.py.ptr->shared(),
                                   particles,
                                   dirty);
      particles.particlesDirty = true;
    }
  }
#endif
}

static void UpdateOverlayState(ParticleArray& particles,
                               const CameraContext& camera,
                               ParticleLabelRenderState& state,
                               OverlayState& overlay)
{
  auto& labels = overlay.particleLabels;

  if (!state.show) {
    labels.clear();
    return;
  }

  if (labels.needsRebuild(particles, camera, state)) {
    labels.rebuild(particles, camera, state);
    particles.flagParticleIndexDirty = false;
  }
}

#ifdef GEOMETRICAL_ANALYSIS
static void RebuildDiskDerivedState(DiskAnalysisResultState& result,
                                    RenderLayerState& diskRenderState,
                                    DiskManager& disks)
{
  if (!result.cpuUpdated) {
    return;
  }

  disks.clearGroup("main_disk");

  if (result.valid) {
    DiskObject disk = result.disk;
    disk.opacity = diskRenderState.opacity;
    disk.color   = glm::vec3(1.0f);
    disk.tag     = "main_disk";
    disks.add(disk);
    diskRenderState.show = true;
  }

  diskRenderState.cpuUpdated = true;
  result.cpuUpdated = false;
}

static void RebuildEllipsoidDerivedState(EllipsoidAnalysisResultState& result,
                                         RenderLayerState& ellipsoidState,
                                         EllipsoidManager& ellipsoids)
{
  if (!result.cpuUpdated) {
    return;
  }

  ellipsoids.clearGroup("analysis_ellipsoid");

  if (result.valid) {
    EllipsoidObject obj = result.ellipsoid;
    obj.opacity = ellipsoidState.opacity;
    obj.color   = glm::vec3(1.0f);
    obj.tag     = "analysis_ellipsoid";
    obj.renderMode = EllipsoidRenderMode::Solid;
    ellipsoids.add(obj);
    ellipsoidState.show = true;
  } else {
    ellipsoidState.show = false;
  }

  ellipsoidState.cpuUpdated = true;
  result.cpuUpdated = false;
}
#endif

#ifdef STREAM_LINE
static void RebuildStreamlinePreviewDerivedState(StreamlinePreviewResultState& result,
                                                 RenderLayerState& cubeRenderState,
                                                 CubeManager& cubes)
{
  if (!result.cpuUpdated) {
    return;
  }

  cubes.clearGroup("streamline_seed_region");

  if (result.valid) {
    CubeObject cube = result.cube;
    cube.tag = "streamline_seed_region";
    cubes.add(cube);
    cubeRenderState.show = true;
  }

  cubeRenderState.cpuUpdated = true;
  result.cpuUpdated = false;
}

static void RebuildStreamlineDerivedState(StreamlineBuildResultState& result,
                                          RenderLayerState& lineRenderState,
                                          LineManager& lines)
{
  if (!result.cpuUpdated) {
    return;
  }

  lines.clearGroup("streamline");

  for (auto& line : result.lines) {
    lines.add(line);
  }

  lineRenderState.show = !result.lines.empty();
  lineRenderState.cpuUpdated = true;
  result.cpuUpdated = false;
}
#endif

static void PropagateDirtyFlags(const ParticleArray& particles,
                                RenderSystem& rs)
{
  if (particles.particlesDirty) {
    rs.resources.particleRenderDataDirty = true;
  }

  if (particles.velocityDirty) {
    rs.resources.velocityInstanceDataDirty = true;
  }
}

static void UpdateParticleRenderResources(ParticleArray& particles,
                                          const ParticleVisualConfig& particleVisual,
                                          const VelocityRenderState& velocityState,
                                          RenderSystem& rs)
{
  if (rs.resources.particleRenderDataDirty) {
    BuildRenderParticles(particles,
                         particleVisual,
                         rs.resources.renderParticles);
    rs.resources.particleRenderDataDirty = false;
    rs.resources.particlesGpuDirty = true;
    particles.particlesDirty = false;
  }

  if (rs.resources.velocityInstanceDataDirty) {
    UpdateVelocityRenderData(particles,
                             velocityState.subtraction,
                             rs.resources.velocityInstanceData);
    rs.resources.velocityInstanceDataDirty = false;
    rs.resources.velocityGpuDirty = true;
    particles.velocityDirty = false;
  }
}

static void UpdateSceneRenderResources(const SceneManagers& scene,
				       RenderRuntimeState& render,
				       RenderSystem& rs)
{
  if (render.cubes.cpuUpdated) {
    BuildCubeRenderData(scene.cube, rs.resources.cubeRenderData);
    render.cubes.cpuUpdated = false;
    rs.resources.cubesGpuDirty = true;
  }

  if (rs.resources.ellipsoidRenderDataDirty) {
    BuildEllipsoidRenderData(scene.ellipsoid,
                             rs.resources.ellipsoidRenderData);
    rs.resources.ellipsoidRenderDataDirty = false;
    rs.resources.ellipsoidsGpuDirty = true;
  }

  if (rs.resources.diskRenderDataDirty) {
    BuildDiskRenderData(scene.disk, rs.resources.diskRenderData);
    rs.resources.diskRenderDataDirty = false;
    rs.resources.disksGpuDirty = true;
  }

  if (rs.resources.lineRenderDataDirty) {
    BuildLineRenderData(scene.line, rs.resources.lineRenderData);
    rs.resources.lineRenderDataDirty = false;
    rs.resources.linesGpuDirty = true;
  }
}

#ifdef ISO_CONTOUR
static void UpdateIsoContourRenderResources(const IsoContourGeometryState& isoContour,
                                            RenderRuntimeState& render,
                                            RenderSystem& rs)
{
  if (!render.isocontour.cpuUpdated) {
    return;
  }

  BuildIsoContourRenderData(isoContour.verts,
                            isoContour.inds,
                            rs.resources.isoContourRenderData);
  render.isocontour.cpuUpdated = false;
  rs.resources.isoContourGpuDirty = true;
}
#endif

#ifdef USE_CONVEX_HULL
static void RebuildConvexHullDerivedState(const ConvexHullRuntimeState& convexState,
                                          RenderLayerState& polyhedraState,
					  PolyhedronManager& polyhedra)
{
  if (!polyhedraState.cpuUpdated) {
    return;
  }

  polyhedra.clearGroup("convex_hull");

  bool anyVisible = false;
  for (const auto& entry : convexState.entries) {
    if (!entry.visible || entry.lineVertices.empty()) {
      continue;
    }

    PolyhedronObject obj;
    obj.vertices.reserve(entry.lineVertices.size() / 3);
    for (size_t k = 0; k + 2 < entry.lineVertices.size(); k += 3) {
      obj.vertices.emplace_back(entry.lineVertices[k],
                                entry.lineVertices[k + 1],
                                entry.lineVertices[k + 2]);
    }

    obj.color   = glm::vec3(1.0f);
    obj.opacity = polyhedraState.opacity;
    obj.tag     = "convex_hull";

    polyhedra.add(entry.sourceId, obj);
    anyVisible = true;
  }

  polyhedraState.show = anyVisible;
  polyhedraState.cpuUpdated = false;
  polyhedraState.gpuUpdated = true;
}

static void UpdateConvexHullRenderState(RenderLayerState& polyhedraState,
					const PolyhedronManager& polyhedra,
					RenderSystem& rs)
{
  if (!polyhedraState.gpuUpdated) {
    return;
  }

  BuildPolyhedronRenderData(polyhedra,
                            rs.resources.polyhedronRenderData);
  
  polyhedraState.gpuUpdated = false; 
  rs.resources.polyhedraGpuDirty = true;
  rs.polyhedron.requestResetGroup("convex_hull");
}
#endif

static void UpdateCuboidAnnotationDerivedState(RenderLayerState& annotationState,
					       RenderLayerState& cuboidsState,
					       RenderLayerState& linesState,
					       CuboidAnnotationManager& annotations,
					       const ProjectionMapToolState& rt)
{
  if (!annotationState.cpuUpdated) {
    return;
  }

  annotations.clear();

  if (annotationState.show) {
    CuboidAnnotationObject obj;
    obj.cuboid = rt.interactiveCuboid;

    obj.cuboid.edgeColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    obj.highlightColor   = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    obj.arrowColor       = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);

    const int axis = rt.params.selectedAxis;
    if (axis == 0)      obj.selectedAxis = CuboidAxis::X;
    else if (axis == 1) obj.selectedAxis = CuboidAxis::Y;
    else                obj.selectedAxis = CuboidAxis::Z;

    obj.showAxisHighlight = true;
    obj.showArrow         = true;
    obj.arrowLength       = 0.2f;
    obj.arrowHeadLength   = 0.05f;
    obj.arrowHeadWidth    = 0.03f;
    obj.tag               = "interactive_cuboid";

    annotations.add(obj);
  }

  cuboidsState.show = annotationState.show;
  linesState.show = annotationState.show;
      
  annotationState.gpuUpdated = true;
  annotationState.cpuUpdated = false;
}


static void UpdateCuboidAnnotationRenderResources(RenderLayerState& annotationState,
						  const CuboidAnnotationManager& annotations,
						  RenderSystem& rs)
{
  if (!annotationState.gpuUpdated) {
    return;
  }

  AppendCuboidAnnotationRenderData(annotations,
				   rs.resources.cuboidRenderData);
  rs.resources.cuboidsGpuDirty = true;
  

  AppendCuboidArrowRenderData(annotations,
			      rs.resources.lineRenderData);
  rs.resources.linesGpuDirty = true;

  annotationState.gpuUpdated = false;
}

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

static ColorbarGizmoState BuildColorbarGizmoState(const RenderRuntimeState& render,
                                                  const ParticleVisualConfig& particleVisual,
                                                  const WindowContext& window)
{
  ColorbarGizmoState state;
  state.visible = render.colorbar.show;

  const int ptype = render.colorbar.sourceParticleType;
  const auto& vis = particleVisual.types[ptype];

  state.content.colormapIndex = vis.colormapIndex;
  state.content.valueMin      = vis.colorMin;
  state.content.valueMax      = vis.colorMax;
  state.content.numTicks      = render.colorbar.numTicks;

  state.effectiveWidth  = static_cast<float>(window.viewportWidth());
  state.effectiveHeight = static_cast<float>(window.viewportHeight());
  state.layout = ComputeColorbarLayout(window, render.colorbar.layout);

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
                            const WindowContext& window,
                            const FrameMatrices& fm)
{
  overlay.particleLabels.draw(fm.view, fm.projection, window);

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
      BuildColorbarGizmoState(render, particleVisual, window);

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

static void RenderScene(const CameraContext& camera,
			const ParticleVisualConfig& particleVisual,
                        const RenderRuntimeState& render,
                        const OverlayState& overlay,
                        RenderSystem& rs,
                        const WindowContext& window)
{
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  FrameMatrices fm = BuildFrameMatrices(camera, window);

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
                  window,
                  fm);
}


static void UpdateProjectionPreview(ProjectionPreviewDerivedState& source,
                                    RenderSystem& render)
{
  if (!source.image.valid()) return;

  if(source.computed){
    render.preview.update(source.image);
    source.computed = false;
  }
  
  ProjectionPreviewUIState previewUI = render.preview.makeUIState();
  DrawProjectionPreviewUI(previewUI);
}

static void MarkPostSnapshotLoad(SnapshotPostprocessState& post)
{
  post.refreshTree = true;
  post.refreshCulling = true;
  post.refreshTopParticles = true;
  post.applyTrackingToCamera = true;
}


static void ProcessSnapshotLoadQueue(AppDataState& data,
                                     AppRuntimeState& runtime)
{
  runtime.snapshotLoad.result = SnapshotLoadResultState{};

  auto& req = runtime.snapshotLoad.request;
  if (!req.pending) return;
  if (data.fileInfo->isLoading()) return;

  auto& src = data.fileInfo->editSource();
  src.currentStep = req.targetStep;
  const int newFileIndex = src.initialIndex + src.currentStep * src.skipStep;
  src.currentFileIndex = newFileIndex;

  if (req.kind == SnapshotLoadKind::GenerateTestData) {
    data.fileInfo->generateTestData(data.particles,
                                    data.header,
                                    runtime.settings.normalization);
  } else {
    data.fileInfo->loadNewSnapshot(newFileIndex,
                                   data.particles,
                                   data.header,
                                   runtime.settings.normalization,
                                   runtime.settings.inputFilter);
  }
  
  runtime.snapshotLoad.result.loadedThisFrame = true;
  runtime.snapshotLoad.result.loadedStep = src.currentStep;
  runtime.snapshotLoad.result.owner = req.owner;

  MarkPostSnapshotLoad(runtime.settings.snapshotPostprocess);    
  req = SnapshotLoadRequestState{};
}


static void BeginFrame(AppRuntimeState& runtime, WindowContext& window)
{
  float currentFrame = static_cast<float>(glfwGetTime());
  float deltaTime = runtime.interaction.beginFrame(currentFrame);
  (void)deltaTime;

  processInput(window.handle());
  glfwPollEvents();
  BeginImGuiFrame();
}

static void EndFrame(WindowContext& window)
{
  EndImGuiFrame();
  glfwSwapBuffers(window.handle());
}

static void RebuildDerivedState(ParticleArray& particles,
                                CameraContext& camera,
                                RenderRuntimeState& render,				
                                AppDerivedState& derived,
				const ProjectionMapToolState& projection)
{
#ifdef GEOMETRICAL_ANALYSIS
  RebuildDiskDerivedState(derived.analysis.disk,
			  render.disks,
			  derived.scene.disk);

  RebuildEllipsoidDerivedState(derived.analysis.ellipsoid,
			       render.ellipsoids,
			       derived.scene.ellipsoid);
#endif

#ifdef STREAM_LINE
  RebuildStreamlinePreviewDerivedState(derived.analysis.streamlinePreview,
				       render.cubes,
				       derived.scene.cube);
  
  RebuildStreamlineDerivedState(derived.analysis.streamlineBuild,
				render.lines,
				derived.scene.line);
#endif
  
  UpdateOverlayState(particles,
                     camera,
                     render.particleLabels,
                     derived.overlay);

#ifdef USE_CONVEX_HULL
  RebuildConvexHullDerivedState(derived.analysis.convexHulls,
                                render.polyhedra,
                                derived.scene.polyhedron);
#endif

  UpdateCuboidAnnotationDerivedState(render.cuboidAnnotations,
				     render.cuboids,
				     render.lines,
				     derived.scene.cuboidAnnotation,
				     projection
				     );
}

static void UpdateRenderResources(ParticleArray& particles,
                                  const ParticleVisualConfig& particleVisual,
                                  RenderRuntimeState& render,
                                  const AppDerivedState& derived,
                                  RenderSystem& rs)
{
  rs.resources.cuboidRenderData.clear();
  rs.resources.lineRenderData.clear();

  PropagateDirtyFlags(particles, rs);

  UpdateParticleRenderResources(particles,
                                particleVisual,
                                render.velocity,
                                rs);

  UpdateSceneRenderResources(derived.scene,
                             render,
                             rs);

#ifdef ISO_CONTOUR
  UpdateIsoContourRenderResources(derived.analysis.isoContour,
                                  render,
                                  rs);
#endif
  
#ifdef USE_CONVEX_HULL
  UpdateConvexHullRenderState(render.polyhedra,
                              derived.scene.polyhedron,
                              rs);
#endif
  
  UpdateCuboidAnnotationRenderResources(render.cuboidAnnotations,
                                        derived.scene.cuboidAnnotation,
                                        rs);
}

static void ExecuteAnalysisRequests(AppDataState& data,
                                    AppRuntimeState& runtime,
                                    AnalysisDerivedState& analysis,
                                    AppServices& services,
				    CameraContext& camera)
{
#ifdef GEOMETRICAL_ANALYSIS
  ExecuteSingleDiskAnalysisRequest(*data.particles,
				   runtime.settings.normalization,
                                   *services.diskFinder,
                                   runtime.analysis.disk,
                                   analysis.disk);
  
  ExecuteDiskBatchRequest(*data.particles,
			  data.header,
			  runtime.settings.normalization,
			  runtime.settings.inputFilter,
                          *data.fileInfo,
                          *services.diskFinder,
                          runtime.render.disks,
                          runtime.analysis.diskBatch,
                          analysis.diskBatch);

  ExecuteSingleEllipsoidAnalysisRequest(*data.particles,
                                        *services.ellipsoid,
                                        runtime.analysis.ellipsoid,
                                        analysis.ellipsoid);
  
  ExecuteEllipsoidBatchRequest(*data.particles,
			       data.header,
			       runtime.settings.normalization,
			       runtime.settings.inputFilter,
                               *data.fileInfo,
                               *services.ellipsoid,
                               runtime.analysis.ellipsoidBatch,
                               analysis.ellipsoidBatch);
#endif

#ifdef STREAM_LINE
  ExecuteStreamlinePreviewRequest(runtime.analysis.streamlinePreview,
				  analysis.streamlinePreview);
  
  ExecuteStreamlineBuildRequest(*data.particles,
				*services.streamLine,
				runtime.analysis.streamlineBuild,
				analysis.streamlineBuild);
#endif

  ExecuteStellarDensityRequest(*data.particles,
			       runtime.settings.normalization, 
			       runtime.analysis.stellarDensity,
			       data.header.time);

#ifdef ISO_CONTOUR
  ExecuteIsoContourRequest(*data.particles,
			   runtime.analysis.isoContour,
			   analysis.isoContour,
			   runtime.render.isocontour);
#endif

#ifdef CLUMP_DATA_READ
  ExecuteClumpBatchRequest(*data.particles,
			   data.header,
			   runtime.settings.normalization,
			   runtime.settings.inputFilter,
			   *data.fileInfo,
			   *services.clumpFind,
			   runtime.analysis.clumpBatch,
			   analysis.clumpBatch);
#endif

#ifdef USE_CONVEX_HULL
  ExecuteConvexHullRequests(*data.particles,
                            *services.clumpFind,
                            *services.convexHull,
                            analysis.convexHulls,
                            runtime.render.polyhedra);
#endif

  ExecuteFileNavigationRequests(*data.fileInfo,
                                runtime.settings.fileNavigation,
                                runtime.snapshotLoad);

  ExecuteCameraPlacementRequests(*data.particles,
				 runtime.settings.normalization,
				 runtime.settings.viewFilter,
				 camera,
				 runtime.settings,
				 runtime.settings.snapshotPostprocess);

  const auto& src = data.fileInfo->getSource();
  ExecutePostSnapshotLoadActions(*data.particles,
				 data.clumpStore,
				 runtime.settings.normalization,
				 runtime.settings.tracking,
				 camera,
				 runtime.settings.snapshotPostprocess,
				 src.currentFileIndex);

  ExecuteProjectionMapRequests(runtime.analysis.projectionMap,
			       *services.projectionMap2D,
			       *data.particles,
			       runtime.settings.normalization,
			       data.fileInfo->getSource().currentFileIndex,
			       analysis.projectionPreview,
			       data.header.time);

  ExecuteProjectionMovieRequest(*data.particles,
				data.header,
				runtime.settings.normalization,
				runtime.settings.tracking,
				*data.fileInfo,
				*services.projectionMap2D,
				runtime.analysis.projectionMap.params,
				camera,
				runtime.analysis.projectionMovie,
				runtime.snapshotLoad,
				runtime.settings.snapshotPostprocess,
				analysis.projectionMovie);

#ifdef PYTHON_BRIDGE
  ExecutePythonBridgeRequests(*data.particles,
			      services.py,
			      runtime.analysis.py.request,
			      runtime.analysis.py.view);
#endif
}

void RunFrame(AppState& app,
              RenderSystem& render,
              WindowContext& window)
{
  BeginFrame(app.runtime, window);

  DrawMainUI(app.data,
	     app.view,
	     app.runtime,
	     app.derived.analysis,
	     app.ui.toolWindows,
	     app.data.header.time);

  DrawToolWindows(app.data,
		  app.view.camera,
		  app.runtime,
		  app.ui.toolWindows,
		  app.derived.analysis,
		  app.services);
  
  UpdateExternalInputs(app.services, *app.data.particles);

  ProcessSnapshotLoadQueue(app.data, app.runtime);
  ExecuteAnalysisRequests(app.data, app.runtime, app.derived.analysis, app.services, app.view.camera);
  
  RebuildDerivedState(*app.data.particles,
                      app.view.camera,
                      app.runtime.render,
                      app.derived,
		      app.runtime.analysis.projectionMap);
  
  UpdateRenderResources(*app.data.particles,
                        app.view.particleVisual,
                        app.runtime.render,
                        app.derived,
                        render);

  UpdateProjectionPreview(app.derived.analysis.projectionPreview, render);

  RenderScene(app.view.camera,
	      app.view.particleVisual,
              app.runtime.render,
              app.derived.overlay,
              render,
              window);

  EndFrame(window);
}
