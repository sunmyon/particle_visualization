#include "app/app_frame.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include <imgui.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#endif

#include "app/state/app_state.h"
#include "app/execution/analysis_dispatch.h"
#include "app/execution/analysis_execution.h"
#include "app/app_derived_rebuild.h"
#include "app/execution/input_execution.h"
#include "app/execution/projection_execution.h"
#include "app/app_render_sync.h"
#include "app/app_snapshot_load.h"
#include "app/execution/tool_window_dispatch.h"
#include "app/state/normalization_config.h"
#include "app/settings_analysis_requests.h"
#include "app/state/tool_window_commands.h"
#include "data/simulation_dataset.h"
#include "render/render_system.h"

#include "UI/common_ui.h"
#include "UI/tool_window_ui.h"
#include "UI/settings_ui.h"
#include "UI/file_format_dialog.h"
#include "FileIO/snapshot_io_service.h"
#include "image/image_io.h"

#include "render/render_frame.h"
#include "render/render_viewport.h"
#include "platform/local_present.h"

#include "platform/imgui_context.h"
#include "platform/window_context.h"

#include "UI/clump_ui.h"

namespace {

std::filesystem::path MakeRenderSnapshotPath()
{
  std::filesystem::create_directories("render_snapshots");

  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif

  std::ostringstream name;
  name << "render_"
       << std::put_time(&tm, "%Y%m%d_%H%M%S")
       << ".png";
  return std::filesystem::path("render_snapshots") / name.str();
}

void SaveRequestedRenderSnapshot(SettingsActionRequestState& request,
                                 const RenderedFrame& frame)
{
  if (!request.renderSnapshotRequested) {
    return;
  }
  request.renderSnapshotRequested = false;

  if (!frame.valid() || frame.format != RenderedFrameFormat::RGBA8) {
    request.renderSnapshotMessage = "Failed.";
    return;
  }

  const std::filesystem::path path = MakeRenderSnapshotPath();
  if (!WritePngRgba(path.string().c_str(),
                    frame.width,
                    frame.height,
                    frame.pixels)) {
    request.renderSnapshotMessage = "Failed.";
    return;
  }
  request.renderSnapshotMessage = "Saved.";
}

} // namespace

static bool QuerySystemAvailableMemoryBytes(size_t& outBytes)
{
#if defined(__APPLE__)
  mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
  vm_statistics64_data_t vmstat{};
  if (host_statistics64(mach_host_self(),
                        HOST_VM_INFO64,
                        reinterpret_cast<host_info64_t>(&vmstat),
                        &count) != KERN_SUCCESS) {
    return false;
  }
  vm_size_t pageSize = 0;
  if (host_page_size(mach_host_self(), &pageSize) != KERN_SUCCESS) {
    return false;
  }
  const auto availablePages =
    static_cast<size_t>(vmstat.free_count) +
    static_cast<size_t>(vmstat.inactive_count);
  outBytes = availablePages * static_cast<size_t>(pageSize);
  return true;
#elif defined(__linux__)
  std::ifstream meminfo("/proc/meminfo");
  std::string key;
  std::string unit;
  size_t valueKiB = 0;
  while (meminfo >> key >> valueKiB >> unit) {
    if (key == "MemAvailable:") {
      outBytes = valueKiB * 1024u;
      return true;
    }
  }

  struct sysinfo info {};
  if (sysinfo(&info) != 0) {
    return false;
  }
  outBytes = static_cast<size_t>(info.freeram) *
             static_cast<size_t>(info.mem_unit);
  return true;
#else
  (void)outBytes;
  return false;
#endif
}
#include "projection/projection_map_ui_state.h"

#ifdef PYTHON_BRIDGE
#include "PythonBridge/BridgeAdapter.h"
#include "PythonBridge/PythonBridge.h"
#include "PythonBridge/ShmLayout.h"
#endif

static RenderViewport MakeRenderViewport(const WindowContext& window)
{
  RenderViewport viewport;
  viewport.x = window.viewportX();
  viewport.y = window.viewportY();
  viewport.width = window.viewportWidth();
  viewport.height = window.viewportHeight();
  viewport.framebufferWidth = window.framebufferWidth();
  viewport.framebufferHeight = window.framebufferHeight();

  const ImGuiIO& io = ImGui::GetIO();
  viewport.framebufferScaleX = io.DisplayFramebufferScale.x;
  viewport.framebufferScaleY = io.DisplayFramebufferScale.y;
  const int imguiFramebufferWidth =
    static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x);
  const int imguiFramebufferHeight =
    static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y);
  if (imguiFramebufferWidth > 0) {
    viewport.framebufferWidth = imguiFramebufferWidth;
  }
  if (imguiFramebufferHeight > 0) {
    viewport.framebufferHeight = imguiFramebufferHeight;
  }

  return viewport;
}

static SettingsViewContext MakeSettingsViewContext(const AppViewState& view,
                                                   const AppRuntimeState& runtime,
                                                   const AnalysisDerivedState& analysis,
                                                   const AppDataState& data,
                                                   const RenderSystem& renderSystem,
                                                   const RenderViewport& viewport)
{
  SettingsViewContext ctx;
  ctx.camera.position[0] = view.camera.cameraPos.x;
  ctx.camera.position[1] = view.camera.cameraPos.y;
  ctx.camera.position[2] = view.camera.cameraPos.z;
  ctx.camera.target[0] = view.camera.cameraTarget.x;
  ctx.camera.target[1] = view.camera.cameraTarget.y;
  ctx.camera.target[2] = view.camera.cameraTarget.z;
  float toOriginal = runtime.settings.normalization.toPhysicalScale();
  if (data.particles && data.particles->simulationBlock.worldToRenderScale > 0.0f) {
    toOriginal = 1.0f / data.particles->simulationBlock.worldToRenderScale;
  }
  ctx.camera.renderToWorldScale = toOriginal;
  for (int axis = 0; axis < 3; ++axis) {
    ctx.camera.originalPosition[axis] = ctx.camera.position[axis] * toOriginal;
    ctx.camera.originalTarget[axis] = ctx.camera.target[axis] * toOriginal;
  }
  ctx.snapshotLoading = runtime.snapshotLoad.busy;
  if (renderSystem.backend) {
    ctx.backend = renderSystem.backend->capabilities();
  }

  if (data.particles) {
    ctx.memory.particleCount =
      data.particles->simulationBlock.particles.size();
    ctx.memory.cpuParticleBytes =
      data.particles->simulationBlock.particles.size() * sizeof(SimulationElement) +
      data.particles->flag_mask.size() * sizeof(uint8_t) +
      data.particles->flag_stress.size() * sizeof(uint8_t);
  }
  ctx.memory.renderParticleCount = renderSystem.scene.particles.size();
  ctx.memory.particleLodProxyCount =
    renderSystem.scene.particleLodProxy.size();
  ctx.memory.particleLodNodeCount = renderSystem.scene.particleLod.nodes.size();
  ctx.memory.cpuRenderSceneBytes =
    renderSystem.scene.particles.size() * sizeof(RenderParticle) +
    renderSystem.scene.particleLodProxy.size() * sizeof(RenderParticle) +
    renderSystem.scene.velocityInstances.size() * sizeof(float);
  ctx.memory.gpuParticleBufferBytes =
    renderSystem.scene.particles.size() * sizeof(RenderParticle);
  ctx.memory.cpuParticleLodTreeBytes =
    EstimateParticleLodTreeBytes(renderSystem.scene.particleLod);
  ctx.memory.systemAvailableKnown =
    QuerySystemAvailableMemoryBytes(ctx.memory.systemAvailableBytes);

  if (ctx.backend.particleFrameCache &&
      runtime.render.scheduling.cacheParticleFrames &&
      viewport.width > 0 &&
      viewport.height > 0) {
    const size_t pixels =
      static_cast<size_t>(viewport.width) * static_cast<size_t>(viewport.height);
    ctx.memory.gpuParticleCacheBytes =
      pixels * (8 + 4); // RGBA16F + DEPTH24/32 estimate.
  }

#ifdef VOLUME_RENDERING
  ctx.memory.volumeNodeCount = renderSystem.scene.volume.nodes.size();
  ctx.memory.gpuVolumeTreeBytes =
    renderSystem.scene.volume.nodes.size() *
    (sizeof(float) * 16 + sizeof(int) * 8);
  if (ctx.backend.volumeFrameCache &&
      runtime.render.scheduling.cacheVolumeFrames &&
      viewport.width > 0 &&
      viewport.height > 0) {
    const float scale =
      std::clamp(runtime.render.scheduling.volumeFrameCacheScale, 0.25f, 1.0f);
    const size_t pixels =
      static_cast<size_t>(
        std::max(1, static_cast<int>(std::ceil(viewport.width * scale)))) *
      static_cast<size_t>(
        std::max(1, static_cast<int>(std::ceil(viewport.height * scale))));
    ctx.memory.gpuVolumeCacheBytes = pixels * 8; // RGBA16F estimate.
  }
#endif

  if (renderSystem.backend && ctx.backend.gpuMemoryQuery) {
    const RenderBackendMemoryInfo backendMemory =
      renderSystem.backend->queryMemoryInfo();
    ctx.memory.gpuAvailableKnown = backendMemory.gpuAvailableKnown;
    ctx.memory.gpuAvailableBytes = backendMemory.gpuAvailableBytes;
  }
  if (renderSystem.backend) {
    ctx.memory.timing = renderSystem.backend->queryTimingInfo();
  }

#ifdef CLUMP_DATA_READ
  ctx.analysis.clumpBatch = &analysis.clumpBatch;
#endif
  ctx.analysis.disk = &analysis.disk;
  ctx.analysis.diskBatch = &analysis.diskBatch;
  ctx.analysis.ellipsoid = &analysis.ellipsoid;
  ctx.analysis.ellipsoidBatch = &analysis.ellipsoidBatch;
#ifdef STREAM_LINE
  ctx.analysis.streamlineBuild = &analysis.streamlineBuild;
#endif
#ifdef ISO_CONTOUR
  ctx.analysis.isoContour = &analysis.isoContour;
#endif
#ifdef VOLUME_RENDERING
  ctx.analysis.volume = &analysis.volume;
#endif
  ctx.analysis.projectionMovie = &analysis.projectionMovie;
#ifdef PYTHON_BRIDGE
  ctx.pythonBridge = &runtime.analysisView.py;
#endif

  return ctx;
}

static void DrawSettingsPanels(AppViewState& view,
                               AppRuntimeState& runtime,
                               AnalysisDerivedState& analysis,
                               AppDataState& data,
                               RenderSystem& renderSystem,
                               const RenderViewport& viewport,
                               SettingsUIState& settingsUI,
                               WindowCommandQueue& windowCommands)
{
  SettingsViewContext settingsView =
    MakeSettingsViewContext(view, runtime, analysis, data, renderSystem, viewport);
  SyncSettingsAnalysisDraftsFromRuntime(settingsUI.analysisEdit,
                                        runtime.analysisRequests);
  ShowSettingsUI(settingsUI,
                 runtime.settings,
                 runtime.analysisJobs,
                 runtime.render,
                 runtime.particleVisual,
                 runtime.quantity,
                 settingsView,
                 windowCommands);
  ShowCameraSettingsUI();
}

static void DrawMainUI(AppViewState& view,
		       AppRuntimeState& runtime,
		       AnalysisDerivedState& analysis,
                       AppDataState& data,
                       RenderSystem& render,
                       const RenderViewport& viewport,
                       SettingsUIState& settingsUI,
                       WindowCommandQueue& windowCommands,
		       const SnapshotCurrentState& current)
{
  ShowTime(current);
  DrawSettingsPanels(view,
                     runtime,
                     analysis,
                     data,
                     render,
                     viewport,
                     settingsUI,
                     windowCommands);
}

static void DrawToolWindows(AppRuntimeState& runtime,
			    const AppViewState& view,
                            const AppDataState& data,
			    ToolWindowUIState& tools,
                            WindowCommandQueue& windowCommands,
                            const RadialProfileResultState& radialProfileResult,
                            const Histogram2DResultState& histogram2DResult,
                            const ProjectionPreviewUIState& projectionPreview)
{
  PlotBatchExportViewContext plotExportCtx;
  plotExportCtx.snapshotFolderPath = runtime.settings.fileNavigation.input.folderPath;
  plotExportCtx.snapshotFileFormat = runtime.settings.fileNavigation.input.fileFormat;
  plotExportCtx.initialIndex =
    runtime.settings.fileNavigation.navigation.initialIndex;
  plotExportCtx.currentStep =
    runtime.settings.fileNavigation.navigation.currentStep;
  plotExportCtx.skipStep =
    runtime.settings.fileNavigation.navigation.skipStep;
  plotExportCtx.batchSize =
    runtime.settings.fileNavigation.navigation.batchSize;
#ifdef HAVE_HDF5
  plotExportCtx.useHDF5 = runtime.settings.fileNavigation.input.useHDF5;
#endif
  float toOriginal = runtime.settings.normalization.toPhysicalScale();
  if (data.particles && data.particles->simulationBlock.worldToRenderScale > 0.0f) {
    toOriginal = 1.0f / data.particles->simulationBlock.worldToRenderScale;
  }
  plotExportCtx.renderToWorldScale = toOriginal;
  plotExportCtx.cameraPosition[0] = view.camera.cameraPos.x * toOriginal;
  plotExportCtx.cameraPosition[1] = view.camera.cameraPos.y * toOriginal;
  plotExportCtx.cameraPosition[2] = view.camera.cameraPos.z * toOriginal;
  plotExportCtx.cameraTarget[0] = view.camera.cameraTarget.x * toOriginal;
  plotExportCtx.cameraTarget[1] = view.camera.cameraTarget.y * toOriginal;
  plotExportCtx.cameraTarget[2] = view.camera.cameraTarget.z * toOriginal;

  TopParticlesViewContext topParticlesCtx;
  topParticlesCtx.quantity = &runtime.quantity;
  topParticlesCtx.exportContext = plotExportCtx;
  DrawTopParticlesUI(tools.topParticles,
                     tools.topParticlesRequest,
                     tools.topParticlesResult,
                     topParticlesCtx);
  
  DrawBinaryFormatDialog(tools.fileFormatDialog,
                         runtime.settings.snapshotFormat.formatTokens);
#ifdef HAVE_HDF5
  DrawHDF5FormatDialog(tools.fileFormatDialog,
                       runtime.settings.snapshotFormat.formatTokensHdf5);
#endif
  DrawMaskWindow(tools.mask,
                 tools.maskRequest,
                 runtime.settings.inputFilter.mask);

  RadialProfileViewContext radialProfileCtx{runtime.quantity, plotExportCtx};
  DrawRadialProfileUI(tools.radialProfile,
                      tools.radialProfileRequest,
                      radialProfileResult,
                      radialProfileCtx);

  ProjectionMapViewContext projectionMapCtx{
    windowCommands,
    runtime.analysisTools.projectionMap,
    runtime.settings.normalization
  };
  DrawProjectionMapUI(tools.projectionMap,
                      runtime.analysisRequests.projectionMapRequest,
                      projectionMapCtx);
  
  DrawProjectionFontSelectionUI(tools.projectionMap,
                                tools.projectionFontSelectionRequest);
  
#ifdef HAVE_HDF5
  HaloesViewContext haloesCtx{plotExportCtx};
  DrawHaloesUI(tools.haloes,
               tools.haloesRequest,
               haloesCtx);
#endif
  
  Histogram2DViewContext histogram2DCtx{runtime.quantity.catalog, plotExportCtx};
  DrawHistogram2DUI(tools.histogram2D,
                    tools.histogram2DRequest,
		    histogram2DResult,
		    histogram2DCtx);

  DrawClumpFinderUI(tools.clumpFind, plotExportCtx);
  
  DrawClumpListUI(tools.clumpList, plotExportCtx);
  
  DrawClumpChainListUI(tools.clumpChain,
		       runtime.settings.fileNavigation.navigation,
                       runtime.settings.fileNavigation.current,
                       plotExportCtx);

  DrawProjectionPreviewUI(tools.projectionMap, projectionPreview);
}

static void UpdateExternalInputs(AppServices& services,
                                 SimulationDataset& particles)
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

static void ExecuteSettingsWindowOpenRequests(SettingsRuntimeState& settings,
                                              ToolWindowUIState& tools,
                                              WindowCommandQueue& windowCommands)
{
  auto& fileNavReq = settings.fileNavigation.request;

#ifdef HAVE_HDF5
  if (fileNavReq.openHDF5FormatDialogRequested) {
    tools.fileFormatDialog.formatTokensEdit =
      settings.snapshotFormat.formatTokensHdf5;
    windowCommands.open(WindowId::HDF5FormatDialog);
    fileNavReq.openHDF5FormatDialogRequested = false;
  }
#endif

  if (fileNavReq.openFormatDialogRequested) {
    tools.fileFormatDialog.formatTokensEdit =
      settings.snapshotFormat.formatTokens;
    windowCommands.open(WindowId::FileFormatDialog);
    fileNavReq.openFormatDialogRequested = false;
  }
}

static void BeginFrame(AppRuntimeState& runtime, WindowContext& window)
{
  float currentFrame = static_cast<float>(window.timeSeconds());
  float deltaTime = runtime.interaction.beginFrame(currentFrame);
  (void)deltaTime;

  window.pollEvents();
  BeginImGuiFrame(window.framebufferWidth(), window.framebufferHeight());
}

static void UpdateRenderInteractionActivity(AppRuntimeState& runtime,
                                            float currentTime)
{
  auto& scheduling = runtime.render.scheduling;
  scheduling.interactionActive =
    scheduling.responsiveInteraction &&
    runtime.interaction.inputActive(currentTime,
                                    scheduling.settleDelaySeconds);
}

static int CurrentFileIndexForRequests(const FileNavigationRuntimeState& fileNav)
{
  if (fileNav.current.loadedFileIndex >= 0) {
    return fileNav.current.loadedFileIndex;
  }
  return fileNav.navigation.currentFileIndex;
}

static void ExecuteSettingsAndNavigationRequests(AppDataState& data,
                                                 AppRuntimeState& runtime,
                                                 CameraContext& camera)
{
  ExecuteFileNavigationRequests(runtime.settings.fileNavigation,
                                runtime.snapshotLoad);

  ExecuteSettingsActionRequests(*data.particles,
                                runtime.quantity,
                                runtime.particleVisual,
                                runtime.render,
                                runtime.settings,
                                runtime.snapshotPostprocess);

  ExecuteCameraPlacementRequests(*data.particles,
				 runtime.settings.normalization,
				 runtime.settings.viewFilter,
				 camera,
				 runtime.settings,
				 runtime.snapshotPostprocess);
}

static void ExecutePostSnapshotLoadPhase(AppDataState& data,
                                         AppRuntimeState& runtime,
                                         CameraContext& camera)
{
  const int currentFileIndex =
    CurrentFileIndexForRequests(runtime.settings.fileNavigation);
  ExecutePostSnapshotLoadActions(*data.particles,
				 data.clumpStore,
				 runtime.settings.normalization,
				 runtime.settings.tracking,
				 camera,
				 runtime.snapshotPostprocess,
                                 currentFileIndex);
}

static void ExecuteExternalServiceRequests(AppDataState& data,
                                           AppRuntimeState& runtime,
                                           AppServices& services)
{
#ifdef PYTHON_BRIDGE
  ExecutePythonBridgeRequests(*data.particles,
			      services.py,
			      runtime.analysisRequests.py,
			      runtime.analysisView.py);
#else
  (void)data;
  (void)runtime;
  (void)services;
#endif
}

static void ExecuteRequests(AppDataState& data,
                            AppRuntimeState& runtime,
                            AnalysisDerivedState& analysis,
                            ToolWindowUIState& tools,
                            AppServices& services,
			    CameraContext& camera)
{
  const int currentFileIndex =
    CurrentFileIndexForRequests(runtime.settings.fileNavigation);

  ParticleToolExecutionInput particleToolInput{
    *data.particles,
    camera,
    runtime.settings.tracking,
    runtime.snapshotPostprocess,
    runtime.quantity
  };

  AnalysisToolExecutionInput analysisToolInput{
    *data.particles,
    runtime.quantity,
    runtime.settings.normalization,
    analysis,
    camera
  };

  ProjectionToolExecutionInput projectionToolInput{
    services.projectionMap2D.get(),
    services.clumpChain.get(),
    *data.particles,
    runtime.quantity.units,
    runtime.analysisTools.projectionMap,
    runtime.settings.fileNavigation.current,
    runtime.snapshotLoad,
    camera,
    runtime.settings.normalization
  };

  HaloToolExecutionInput haloToolInput{
    data.haloStore,
    *data.particles,
    camera,
    runtime.settings.normalization
  };

  ClumpToolExecutionInput clumpToolInput{
    services.clumpFind.get(),
    services.clumpLoad.get(),
    data.clumpStore,
    *data.particles,
    runtime.settings.tracking,
    camera,
    runtime.settings.fileNavigation.input,
    runtime.settings.fileNavigation.current,
    runtime.settings.normalization,
    currentFileIndex
  };
  ExecuteParticleToolRequests(tools, particleToolInput);
  ExecuteAnalysisToolRequests(tools, analysisToolInput);
  ExecuteProjectionToolRequests(tools, projectionToolInput);
  ExecuteDataFilterToolRequests(tools);
  ExecuteHaloToolRequests(tools, haloToolInput);
  ExecuteClumpToolRequests(tools, clumpToolInput);

  ExecuteSettingsAndNavigationRequests(data,
                                       runtime,
                                       camera);

  ExecuteAnalysisJobRequests(data,
                             runtime,
                             analysis,
                             services,
                             camera,
                             currentFileIndex);

  ExecuteExternalServiceRequests(data, runtime, services);
}

void RunFrame(AppState& app,
              RenderSystem& render,
              WindowContext& window,
              IFramePresenter& presenter)
{
  BeginFrame(app.runtime, window);
  const bool captureRenderSnapshot =
    app.runtime.settings.request.renderSnapshotRequested;

  app.view.camera.stopCameraMode =
    app.ui.toolWindows.projectionMap.open &&
    app.ui.toolWindows.projectionMap.selectMode;

  const InputExecutionResult inputResult =
    ExecuteInputEvents(app.runtime.inputEvents,
                       app.runtime.interaction,
                       app.view.camera,
                       app.runtime.settings);
  if (inputResult.closeRequested) {
    window.requestClose();
  }
  if (inputResult.cameraInteraction) {
    app.runtime.interaction.markInputActivity(
      static_cast<float>(window.timeSeconds()));
  }

  app.runtime.snapshotLoad.busy =
    (app.services.snapshotIO && app.services.snapshotIO->isLoading());

  const RenderViewport uiViewport = MakeRenderViewport(window);
  if (!captureRenderSnapshot) {
    DrawMainUI(app.view,
	       app.runtime,
	       app.derived.analysis,
               app.data,
               render,
               uiViewport,
	       app.ui.settings,
               app.ui.windowCommands,
               app.runtime.settings.fileNavigation.current);

    ExecuteSettingsWindowOpenRequests(app.runtime.settings,
                                      app.ui.toolWindows,
                                      app.ui.windowCommands);

    ApplyWindowCommands(app.ui.windowCommands, app.ui.toolWindows);
  }

  const bool supportsProjectionPreview =
    render.backend && render.backend->capabilities().projectionPreview;
  if (!captureRenderSnapshot && supportsProjectionPreview) {
    UpdateProjectionPreviewTexture(app.derived.analysis.projectionPreview, render);
  }
  const ProjectionPreviewUIState projectionPreviewUI =
    supportsProjectionPreview ? render.backend->makeProjectionPreviewUIState()
                              : ProjectionPreviewUIState{};

  if (!captureRenderSnapshot) {
    DrawToolWindows(app.runtime,
                    app.view,
                    app.data,
                    app.ui.toolWindows,
                    app.ui.windowCommands,
                    app.derived.analysis.radial,
                    app.derived.analysis.hist2D,
                    projectionPreviewUI);

    ApplyWindowCommands(app.ui.windowCommands, app.ui.toolWindows);
  }

  UpdateExternalInputs(app.services, *app.data.particles);

  ProcessSnapshotLoadQueue(app.data, app.runtime, app.services, &app.view.camera);
  ExecutePostSnapshotLoadPhase(app.data,
                               app.runtime,
                               app.view.camera);
  SubmitSettingsAnalysisRequests(app.ui.settings.analysisEdit,
                                 app.runtime.analysisRequests);
  ExecuteRequests(app.data,
                  app.runtime,
                  app.derived.analysis,
                  app.ui.toolWindows,
                  app.services,
                  app.view.camera);
  
  const DerivedRebuildResult derivedRebuild =
    RebuildDerivedState(*app.data.particles,
                        app.view.camera,
                        app.derived,
                        app.runtime.render,
		        app.runtime.analysisTools.projectionMap);
  ApplyDerivedRenderInvalidation(derivedRebuild,
                                 app.runtime.render);
  AcknowledgeDerivedRebuild(*app.data.particles,
                            app.derived,
                            derivedRebuild);

  const double currentTime = window.timeSeconds();
  UpdateRenderInteractionActivity(app.runtime,
                                  static_cast<float>(currentTime));
  
  const ParticleRenderInput particleRenderInput =
    MakeParticleRenderInput(*app.data.particles);
  const ParticleRenderBuildResult buildResult =
    UpdateRenderSceneData(particleRenderInput,
                          app.runtime.particleVisual,
                          app.view.camera,
                          currentTime,
                          render.backend && render.backend->capabilities().particles &&
                            render.backend->isSoftwareRenderer(),
                          app.runtime.render,
                          app.derived,
                          render);
  AcknowledgeParticleRenderBuild(*app.data.particles, buildResult);

  const RenderViewport renderViewport = MakeRenderViewport(window);
  RenderRuntimeState frameRender = app.runtime.render;
  OverlayState frameOverlay = app.derived.overlay;
  if (captureRenderSnapshot) {
    const auto& snapshot = app.runtime.settings.request;
    frameRender.colorbar.show =
      frameRender.colorbar.show && snapshot.renderSnapshotShowColorbar;
    frameRender.coordAxes.show =
      frameRender.coordAxes.show && snapshot.renderSnapshotShowCoordAxes;
    frameRender.crossGizmo.show =
      frameRender.crossGizmo.show && snapshot.renderSnapshotShowCrossGizmo;
    frameRender.particleLabels.show =
      frameRender.particleLabels.show && snapshot.renderSnapshotShowParticleLabels;
    if (!snapshot.renderSnapshotShowParticleLabels) {
      frameOverlay.particleLabels.clear();
    }
    if (snapshot.renderSnapshotShowTimeLabel) {
      ShowTime(app.runtime.settings.fileNavigation.current);
    }
  }

  UpdateRenderFrameInput(app.renderFrameInput,
                         renderViewport,
                         app.view.camera,
                         app.runtime.particleVisual,
                         frameRender,
                         frameOverlay);
  PrepareRenderFrame(app.renderFrameInput, render);

  RenderScene(render);

  PresentOptions presentOptions;
  presentOptions.readbackFrame = captureRenderSnapshot;
  const PresentResult presentResult = presenter.present(presentOptions);
  if (captureRenderSnapshot) {
    SaveRequestedRenderSnapshot(app.runtime.settings.request,
                                presentResult.frame);
  }
}
