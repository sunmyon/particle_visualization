#include "app/app_frame.h"

#include <vector>

#include <glad/glad.h>
#include <imgui.h>

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
#include "render/render_system.h"

#include "UI/common_ui.h"
#include "UI/tool_window_ui.h"
#include "UI/settings_ui.h"
#include "FileIO/file_format_dialog.h"
#include "FileIO/snapshot_io_service.h"

#include "render/render_frame.h"
#include "render/render_viewport.h"
#include "platform/local_present.h"

#include "imgui_context.h"
#include "window_context.h"

#include "FindClumps/find_clumps_ui.h"

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
                                                   const AnalysisDerivedState& analysis)
{
  SettingsViewContext ctx;
  ctx.camera.position[0] = view.camera.cameraPos.x;
  ctx.camera.position[1] = view.camera.cameraPos.y;
  ctx.camera.position[2] = view.camera.cameraPos.z;
  ctx.camera.target[0] = view.camera.cameraTarget.x;
  ctx.camera.target[1] = view.camera.cameraTarget.y;
  ctx.camera.target[2] = view.camera.cameraTarget.z;
  ctx.snapshotLoading = runtime.snapshotLoad.busy;

#ifdef CLUMP_DATA_READ
  ctx.analysis.clumpBatch = &analysis.clumpBatch;
#endif
  ctx.analysis.disk = &analysis.disk;
  ctx.analysis.diskBatch = &analysis.diskBatch;
  ctx.analysis.ellipsoid = &analysis.ellipsoid;
  ctx.analysis.ellipsoidBatch = &analysis.ellipsoidBatch;
  ctx.analysis.projectionMovie = &analysis.projectionMovie;
#ifdef PYTHON_BRIDGE
  ctx.pythonBridge = &runtime.analysisView.py;
#endif

  return ctx;
}

static void DrawSettingsPanels(AppViewState& view,
                               AppRuntimeState& runtime,
                               AnalysisDerivedState& analysis,
                               SettingsUIState& settingsUI,
                               WindowCommandQueue& windowCommands)
{
  SettingsViewContext settingsView =
    MakeSettingsViewContext(view, runtime, analysis);
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
                       SettingsUIState& settingsUI,
                       WindowCommandQueue& windowCommands,
		       const SnapshotCurrentState& current)
{
  ShowTime(current);
  DrawSettingsPanels(view, runtime, analysis, settingsUI, windowCommands);
}

static void DrawToolWindows(AppRuntimeState& runtime,
			    ToolWindowUIState& tools,
                            WindowCommandQueue& windowCommands,
                            const RadialProfileResultState& radialProfileResult,
                            const Histogram2DResultState& histogram2DResult,
                            const ProjectionPreviewUIState& projectionPreview)
{
  TopParticlesViewContext topParticlesCtx;
  topParticlesCtx.quantity = &runtime.quantity;
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

  RadialProfileViewContext radialProfileCtx{runtime.quantity};
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
  HaloesViewContext haloesCtx{};
  DrawHaloesUI(tools.haloes,
               tools.haloesRequest,
               haloesCtx);
#endif
  
  Histogram2DViewContext histogram2DCtx{runtime.quantity.catalog};
  DrawHistogram2DUI(tools.histogram2D,
                    tools.histogram2DRequest,
		    histogram2DResult,
		    histogram2DCtx);

  DrawClumpFinderUI(tools.clumpFind);
  
  DrawClumpListUI(tools.clumpList);
  
  DrawClumpChainListUI(tools.clumpChain,
		       runtime.settings.fileNavigation.navigation,
                       runtime.settings.fileNavigation.current);

  DrawProjectionPreviewUI(projectionPreview);
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

  const InputExecutionResult inputResult =
    ExecuteInputEvents(app.runtime.inputEvents,
                       app.runtime.interaction,
                       app.view.camera,
                       app.runtime.settings);
  if (inputResult.closeRequested) {
    window.requestClose();
  }

  app.runtime.snapshotLoad.busy =
    (app.services.snapshotIO && app.services.snapshotIO->isLoading());

  DrawMainUI(app.view,
	     app.runtime,
	     app.derived.analysis,
	     app.ui.settings,
             app.ui.windowCommands,
             app.runtime.settings.fileNavigation.current);

  ExecuteSettingsWindowOpenRequests(app.runtime.settings,
                                    app.ui.toolWindows,
                                    app.ui.windowCommands);

  ApplyWindowCommands(app.ui.windowCommands, app.ui.toolWindows);

  UpdateProjectionPreviewTexture(app.derived.analysis.projectionPreview, render);
  const ProjectionPreviewUIState projectionPreviewUI =
    render.preview.makeUIState();

  DrawToolWindows(app.runtime,
                  app.ui.toolWindows,
                  app.ui.windowCommands,
                  app.derived.analysis.radial,
                  app.derived.analysis.hist2D,
                  projectionPreviewUI);

  ApplyWindowCommands(app.ui.windowCommands, app.ui.toolWindows);

  UpdateExternalInputs(app.services, *app.data.particles);

  ProcessSnapshotLoadQueue(app.data, app.runtime, app.services);
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
                                 app.view.camera,
                                 app.runtime.render);
  AcknowledgeDerivedRebuild(*app.data.particles,
                            app.derived,
                            derivedRebuild);
  
  const ParticleRenderInput particleRenderInput =
    MakeParticleRenderInput(*app.data.particles);
  const ParticleRenderUploadResult uploadResult =
    UpdateRenderResources(particleRenderInput,
                          app.runtime.particleVisual,
                          app.runtime.render,
                          app.derived,
                          render);
  AcknowledgeParticleRenderUploads(*app.data.particles, uploadResult);

  const RenderViewport renderViewport = MakeRenderViewport(window);
  UpdateRenderFrameInput(app.renderFrameInput,
                         renderViewport,
                         app.view.camera,
                         app.runtime.particleVisual,
                         app.runtime.render,
                         app.derived.overlay);
  PrepareRenderFrame(app.renderFrameInput, render);

  RenderScene(render);

  presenter.present();
}
