#include "settings_ui.h"
#include "app/state/analysis_state.h"
#include "app/state/render_runtime_state.h"
#include "app/state/runtime_state.h"
#include "app/state/ui_state.h"
#include "app/state/snapshot_state_sync.h"
#include "app/state/window_commands.h"

#include "interaction/camera.h"
#include "render/particle_visual_config.h"   // Concrete ParticleVisualConfig definition.
#include "UI/file_format_dialog.h"
#include "UI/volume_rendering_ui.h"
#include "render/colormap_defs.h"  

#include <algorithm>
#include <cmath>
#include <cstring>
#include <imgui.h>

#ifndef NONATIVEFILEDIALOG
#include <nfd.h>
#else
#include "ImGuiFileDialog.h" // Match the include path.
#endif


struct PullDownItem {
  const char* label;
  int mode;
};

static void SyncSettingsDraftsFromRuntime(SettingsActionRequestState& request,
                                          const ParticleVisualConfig& particleVisual,
                                          const RenderRuntimeState& render,
                                          const QuantityState& quantity);
static void DrawCameraInfoSection(const SettingsCameraView& camera);
static void DrawParticleTypeSettingsSection(const QuantityState& quantity,
					    SettingsActionRequestState& req);
static void DrawFileNavigationSection(FileNavigationRuntimeState& rt,
                                      SnapshotFormatState& format,
                                      bool isLoading,
                                      WindowCommandQueue& windowCommands);
static void DrawNormalizationSection(NormalizationContext& ctx,
				     SettingsActionRequestState& req);
static void DrawSinkIdSection(const SettingsCameraView& camera,
	                              SettingsActionRequestState& req);
static void DrawCameraPlacementSection(SettingsRuntimeState& rt, const SettingsCameraView& camera);
#ifdef PYTHON_BRIDGE
static bool DrawPythonBridgeSection(SettingsPythonBridgeEdit& edit, const PythonBridgeViewState& view);
#endif

static void DrawAnalysisSection(SettingsAnalysisEditState& edit,
                                const AnalysisJobRuntimeState& jobs,
                                const FileNavigationRuntimeState& fileNav,
                                const SettingsAnalysisResultView& result,
                                SettingsUIState& ui,
                                WindowCommandQueue& windowCommands,
                                SettingsActionRequestState& settingsReq);

static void DrawRenderingSection(const QuantityState& quantity,
				 SettingsAnalysisEditState& edit,
                                 const AnalysisJobRuntimeState& jobs,
                                 const SettingsAnalysisResultView& result,
                                 SettingsUIState& ui,
                                 WindowCommandQueue& windowCommands,
				 SettingsActionRequestState& settingsReq);

static void DrawOtherSettingsSection(SettingsRuntimeState& rt);

void ShowSettingsUI(SettingsUIState& ui,
                    SettingsRuntimeState& settings,
                    const AnalysisJobRuntimeState& analysisJobs,
                    const RenderRuntimeState& render,
                    const ParticleVisualConfig& particleVisual,
                    const QuantityState& quantity,
                    const SettingsViewContext& view,
                    WindowCommandQueue& windowCommands) {
  ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysVerticalScrollbar);

  SyncSettingsDraftsFromRuntime(settings.request,
                                particleVisual,
                                render,
                                quantity);

  DrawCameraInfoSection(view.camera);
  DrawParticleTypeSettingsSection(quantity, settings.request);
  DrawFileNavigationSection(settings.fileNavigation,
                            settings.snapshotFormat,
                            view.snapshotLoading,
                            windowCommands);
  DrawNormalizationSection(settings.normalization, settings.request);
  DrawSinkIdSection(view.camera, settings.request);
  DrawCameraPlacementSection(settings, view.camera);
#ifdef PYTHON_BRIDGE
  if (view.pythonBridge) {
    if (DrawPythonBridgeSection(ui.analysisEdit.py, *view.pythonBridge)) {
      ui.analysisEdit.pyDirty = true;
    }
  }
#endif
  DrawAnalysisSection(ui.analysisEdit,
	                      analysisJobs,
                      settings.fileNavigation,
                      view.analysis,
                      ui,
                      windowCommands,
                      settings.request);
  DrawRenderingSection(quantity,
		                       ui.analysisEdit,
                       analysisJobs,
                       view.analysis,
                       ui,
                       windowCommands,
                       settings.request);
  DrawOtherSettingsSection(settings);

  ImGui::End();
}

static void SyncSettingsDraftsFromRuntime(SettingsActionRequestState& request,
                                          const ParticleVisualConfig& particleVisual,
                                          const RenderRuntimeState& render,
                                          const QuantityState& quantity)
{
  if (!request.particleVisualDraftDirty && !request.applyParticleVisualRequested) {
    request.particleVisualDraft = particleVisual;
  }

  if (!request.renderDraftDirty && !request.applyRenderRequested) {
    request.renderDraft.scheduling = render.scheduling;
    request.renderDraft.particleLabels = render.particleLabels;
    request.renderDraft.velocity = render.velocity;
#ifdef VOLUME_RENDERING
    request.renderDraft.volume = render.volume;
#endif
    request.renderDraft.diskOpacity = render.disks.opacity;
    request.renderDraft.ellipsoidOpacity = render.ellipsoids.opacity;
    request.renderDraft.isoContourOpacity = render.isocontour.opacity;
    request.renderDraft.crossGizmoSize = render.crossGizmo.size;
  }

  if (!request.unitsDraftDirty && !request.applyUnitsRequested) {
    request.unitsDraft = quantity.units;
  }
}

static void DrawCameraInfoSection(const SettingsCameraView& camera) {
  ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)",
              camera.position[0], camera.position[1], camera.position[2]);
  ImGui::Text("Camera Target:   (%.2f, %.2f, %.2f)",
              camera.target[0], camera.target[1], camera.target[2]);
}

static void DrawParticleTypeSettingsSection(const QuantityState& quantity,
					    SettingsActionRequestState& req) {
  if (!ImGui::CollapsingHeader("Particle Type Settings"))
    return;

  for (int i = 0; i < 6; i++) {
    std::string header = "Type " + std::to_string(i);
    if (ImGui::TreeNode(header.c_str())) {
      auto& cfg = req.particleVisualDraft.types[i];
      bool visualChanged = false;
      const ColormapDef* colormaps = AvailableColormaps();
      const int colormapCount = AvailableColormapCount();
				
      std::string comboLabel = "Colormap##" + std::to_string(i);
      const char* preview = colormaps[cfg.colormapIndex].name;
      if (ImGui::BeginCombo(comboLabel.c_str(), preview)) {
	for (int k = 0; k < colormapCount; ++k) {
	  bool selected = (cfg.colormapIndex == k);
	  if (ImGui::Selectable(colormaps[k].name, selected)) {
	    cfg.colormapIndex = k;
	    visualChanged = true;
	  }
	  if (selected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
      }
				
      visualChanged |= ImGui::Checkbox("Periodic Color Bar", &cfg.periodicColorBar);
				
      std::string sliderLabel = "Point Size##" + std::to_string(i);
      visualChanged |= ImGui::SliderFloat(sliderLabel.c_str(), &cfg.pointSize, 1.0f, 100.0f);
      std::string minLabel = "Value Min##" + std::to_string(i);
      visualChanged |= ImGui::InputFloat(minLabel.c_str(), &cfg.colorMin, 0.01f, 0.1f, "%.3f");
      std::string maxLabel = "Value Max##" + std::to_string(i);
      visualChanged |= ImGui::InputFloat(maxLabel.c_str(), &cfg.colorMax, 0.01f, 0.1f, "%.3f");
      std::string logLabel = "Use Log Scale##" + std::to_string(i);
      visualChanged |= ImGui::Checkbox(logLabel.c_str(), &cfg.useLogScale);
				
      std::string hideLabel = "Hide particle##" + std::to_string(i);
      visualChanged |= ImGui::Checkbox(hideLabel.c_str(), &cfg.hideParticles);
				
      QuantityId& sel = cfg.selectedQuantity;

      std::string quantityLabel = "Quantity##ptype_" + std::to_string(i);
      if (ImGui::BeginCombo(quantityLabel.c_str(), QuantityLabel(sel))) {
	for (int q = 0; q < quantity.catalog.nUIQ; ++q) {
	  QuantityId cand = quantity.catalog.uiQ[q];
	  bool is_selected = (cand == sel);
	  if (ImGui::Selectable(QuantityLabel(cand), is_selected)) {
	    sel = cand;
	    visualChanged = true;
	  }
	  if (is_selected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
      }
				
      const int qidx = static_cast<int>(sel);
      ImGui::Text("Current particle %s range: [%g, %g]",
		  QuantityLabel(sel),
		  quantity.range.valueMin[qidx][i],
		  quantity.range.valueMax[qidx][i]);
				
      if (visualChanged) {
        req.particleVisualDraftDirty = true;
        req.applyParticleVisualRequested = true;
	req.particleRenderDirtyRequested = true;
      }
				
      ImGui::TreePop();
    }
  }    
}

static void DrawFileNavigationSection(FileNavigationRuntimeState& rt,
                                      SnapshotFormatState& format,
                                      bool isLoading,
                                      WindowCommandQueue& windowCommands){
  if(!ImGui::CollapsingHeader("File Navigation"))
    return;

  auto& nav = rt.navigation;
  auto& input = rt.input;
  
  ImGui::InputText("Folder", input.folderPath, IM_ARRAYSIZE(input.folderPath));
  ImGui::InputText("File Format", input.fileFormat, IM_ARRAYSIZE(input.fileFormat));
  ImGui::InputInt("initialIndex", &nav.initialIndex);
		
  RefreshSnapshotFilePath(rt);
		
  if (ImGui::Button("Browse Files")) {
#ifndef NONATIVEFILEDIALOG
    nfdu8char_t* outPath = nullptr;

    nfdopendialogu8args_t args = {};
    //args.filterList  = filters;
    //args.filterCount = 1;
    args.filterList  = nullptr; 
    args.filterCount = 0;    
    args.defaultPath = input.folderPath[0] ? input.folderPath : nullptr;

    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY) {
      ApplySelectedSnapshotPath(rt, outPath);
      NFD_FreePathU8(outPath);
    }
    else if (result == NFD_CANCEL) {
    }
    else {
      std::cerr << "Error: " << NFD_GetError() << std::endl;
    }
#else
    IGFD::FileDialogConfig config;
    // Set the initial directory via the "path" member.
    //config.path = src.filePath;
    config.path = input.folderPath;
    // Set an initial filename if needed; empty waits for user input.
    config.fileName = "output"; 
    // Leave other options such as selectable file count at their defaults.
    ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", "**", config);
#endif
  }
		
#ifdef NONATIVEFILEDIALOG
  if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey"))
    {
      if (ImGuiFileDialog::Instance()->IsOk())
	{
	  std::string selectedFile = ImGuiFileDialog::Instance()->GetFilePathName();
	  ApplySelectedSnapshotPath(rt, selectedFile.c_str());
	}
      else
	{
	  ImGui::Text("File Dialog Cancelled");
	}
      ImGuiFileDialog::Instance()->Close();
    }
#endif
  
  RecomputeCurrentFileIndex(rt);
  ImGui::Text("File: %s", input.filePath);
  ImGui::Text("Current File: %d", nav.currentFileIndex);
		
  ImGui::BeginDisabled(isLoading);

  if (rt.tempSkipStep <= 0) {
    rt.tempSkipStep = nav.skipStep;
  }

  if (ImGui::InputInt("Skip Step", &rt.tempSkipStep, 1, 100)) {
    rt.request.applySkipStepRequested = true;
  }

  if (ImGui::InputInt("Select File Index", &nav.currentStep, 1, 10)) {
    rt.request.loadSelectedSnapshotRequested = true;
  }

  if (ImGui::Button("Previous File") && nav.currentStep > 0) {
    rt.request.loadPreviousRequested = true;
  }

  ImGui::SameLine();

  if (ImGui::Button("Next File")) {
    rt.request.loadNextRequested = true;
  }

  if (ImGui::InputInt("Batch Size", &nav.batchSize)) {
    rt.request.loadBatchRequested = true;
  }

  ImGui::EndDisabled();

  if (isLoading) {
    ImGui::Text("Loading...");
  }

  if (ImGui::Button("Reload")) {
    rt.request.reloadRequested = true;
  }

  if (ImGui::Button("Edit Data Format")) {
#ifdef HAVE_HDF5
    if (input.useHDF5) {
      rt.request.openHDF5FormatDialogRequested = true;
    } else
#endif
    {
      rt.request.openFormatDialogRequested = true;
    }
  }

  static const char* FileFormatNames[] = {
    "Auto", "HDF5", "Binary", "Gadget", "Framed"
  };
  static_assert(static_cast<int>(FileFormat::_Count) == IM_ARRAYSIZE(FileFormatNames),
                "FileFormatNames needs to match FileFormat::_Count");

  int fmtIdx = static_cast<int>(format.readFormat);
  if (ImGui::Combo("Read data format", &fmtIdx, FileFormatNames, IM_ARRAYSIZE(FileFormatNames))) {
    format.readFormat = static_cast<FileFormat>(fmtIdx);
  }

  if (ImGui::Button("Mask Settings...")) {
    windowCommands.open(WindowId::Mask);
  }

  if (ImGui::Button("Generate test data")) {
    rt.request.generateTestDataRequested = true;
  }
}



static void DrawNormalizationSection(NormalizationContext& normalization,
				     SettingsActionRequestState& req) {
  if (!ImGui::CollapsingHeader("Normalization"))
    return;

  ImGui::InputFloat("Desired Maximum", &normalization.desiredMax, 0.f, 0.f, "%g");
  if (ImGui::Button("Normalize Positions")) {
    req.normalizeRequested = true;
  }

  ImGui::Text("Original max coordinate: %.3g", normalization.originalMax);
  ImGui::Text("Max coordinate is normalized to: %.3f", normalization.desiredMax);
}

static void DrawSinkIdSection(const SettingsCameraView& camera,
	                              SettingsActionRequestState& req)
{
  if (!ImGui::CollapsingHeader("set sink ID visualization"))
    return;

  auto& labels = req.renderDraft.particleLabels;
  bool changed = false;

  changed |= ImGui::InputFloat("radius", &labels.queryRadius, 0.f, 0.f, "%g");
  changed |= ImGui::InputInt("number of particles", &labels.maxLabels);

  labels.moveThreshold = 0.1f * labels.queryRadius;

  if (ImGui::Button("show sink IDs")) {
    labels.show = true;
    labels.lastCameraPos = glm::vec3(camera.position[0],
                                     camera.position[1],
                                     camera.position[2]);
    changed = true;
  }

  if (ImGui::Button("disable sink IDs")) {
    labels.show = false;
    changed = true;
  }

  if (changed) {
    req.renderDraftDirty = true;
    req.applyRenderRequested = true;
  }
}

static void DrawCameraPlacementSection(SettingsRuntimeState& rt,
				       const SettingsCameraView& camera)
{
  if (!ImGui::CollapsingHeader("Set camera pos"))
    return;

  auto& req = rt.cameraPlacement;

  ImGui::InputFloat3("Center Coordinates", req.centerInput, "%.3f");
  ImGui::Checkbox("Input in Original Coordinates", &req.inputIsOriginal);

  if (ImGui::Button("Set Center")) {
    req.setCenterRequested = true;
  }

  const char* viewDirections[] = {
    "View from +X", "View from -X",
    "View from +Y", "View from -Y",
    "View from +Z", "View from -Z"
  };
  ImGui::Combo("Projection Direction", &req.currentView, viewDirections, IM_ARRAYSIZE(viewDirections));

  ImGui::SliderFloat("Roll Angle (deg)", &req.rollAngle, -180.0f, 180.0f, "%.1f");

  if (ImGui::Button("Set Projection")) {
    req.setProjectionRequested = true;
  }

  ImGui::SeparatorText("View Culling");    
  ImGui::InputFloat("Culling radius", &rt.viewFilter.radiusCullingSphere, 0.f, 0.f, "%g");
  ImGui::InputFloat3("Culling center", &rt.viewFilter.center.x, "%.3f");

  ImGui::Checkbox("Culling Center in Original Coordinates", &rt.viewFilter.centerIsOriginal);
  ImGui::Checkbox("Culling Radius in Original Coordinates", &rt.viewFilter.radiusIsOriginal);

  if (ImGui::Button("Use Camera Target")) {
    rt.viewFilter.center = glm::vec3(camera.target[0],
                                     camera.target[1],
                                     camera.target[2]);
    rt.viewFilter.centerIsOriginal = false;
  }

  if (ImGui::Button("Apply Culling")) {
    req.applyCullingRequested = true;
  }
  
  if (ImGui::Button("Disable Culling")) {
    req.clearCullingRequested = true;
  }
}

#ifdef PYTHON_BRIDGE
static bool DrawPythonBridgeSection(SettingsPythonBridgeEdit& edit,
                                    const PythonBridgeViewState& view)
{
  if (!ImGui::CollapsingHeader("Python:Jupyter notebook"))
    return false;

  bool changed = false;
  const bool isOpen = view.available;

  if (ImGui::Button(isOpen ? "Close notebook" : "Open notebook")) {
    if (isOpen) {
      edit.shutdownClicked = true;
    } else {
      edit.launchClicked = true;
    }
    changed = true;
  }

  if (view.available) {
    ImGui::SameLine();
    ImGui::TextColored(view.launched ? ImVec4(0.6f,1,0.6f,1)
                                     : ImVec4(1,0.8f,0.4f,1),
                       view.launched ? "launched" : "launching...");
  }

  if (view.launched) {
    ImGui::SeparatorText("Jupyter Notebook");
    ImGui::Text("Port : %d", view.port);
    ImGui::TextWrapped("URL  : %s", view.url.c_str());

    ImGui::SameLine();
    if (ImGui::SmallButton("Copy URL")) {
      ImGui::SetClipboardText(view.url.c_str());
    }

    if (ImGui::SmallButton("Open in Browser")) {
      edit.openBrowserClicked = true;
      changed = true;
    }

    if (!view.lastError.empty()) {
      ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "%s", view.lastError.c_str());
    }
  }

  return changed;
}
#endif

static void DrawAnalysisSection(SettingsAnalysisEditState& edit,
                                const AnalysisJobRuntimeState& jobs,
                                const FileNavigationRuntimeState& fileNav,
                                const SettingsAnalysisResultView& result,
                                SettingsUIState& ui,
                                WindowCommandQueue& windowCommands,
                                SettingsActionRequestState& settingsReq){
  if (!ImGui::CollapsingHeader("Analysis"))
    return;

  enum AnalysisMode {
    ANALYSIS_RADIAL_PROFILE,
    ANALYSIS_2D_HISTOGRAM,
    ANALYSIS_CLUMP_FIND,
    ANALYSIS_STELLAR_DENSITY, 
    ANALYSIS_HALO_CATALOGUE,
    ANALYSIS_POWER_SPEC,
    ANALYSIS_DISK,
    ANALYSIS_ISO_DENSITY
  };
		
  static PullDownItem analysisItems[] = {
    { "radial profile", ANALYSIS_RADIAL_PROFILE },
      { "2D histogram", ANALYSIS_2D_HISTOGRAM },
	{ "clump finder", ANALYSIS_CLUMP_FIND },
	{ "stellar density", ANALYSIS_STELLAR_DENSITY },
	{ "halo catalogue", ANALYSIS_HALO_CATALOGUE },
#ifdef POWER_SPECTRUM
	{ "power spectrum", ANALYSIS_POWER_SPEC },
#endif
#ifdef GEOMETRICAL_ANALYSIS
	{ "extract disks", ANALYSIS_DISK },
	{ "extract iso density", ANALYSIS_ISO_DENSITY },
#endif
  };
		
  // Find the currently selected label.
  const char* currentLabel = "unknown";
  for (const auto& item : analysisItems) {
    if (item.mode == ui.analysisMode) {
      currentLabel = item.label;
      break;
    }
  }
		
  if (ImGui::BeginCombo("Analysis mode", currentLabel)) {
    for (const auto& item : analysisItems) {
      bool isSelected = (ui.analysisMode == item.mode);
      if (ImGui::Selectable(item.label, isSelected)) {
	ui.analysisMode = item.mode;
      }
      if (isSelected)
	ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
		
  switch (ui.analysisMode) {  
  case ANALYSIS_RADIAL_PROFILE: {
    if (ImGui::Button("Compute radial profile"))
      windowCommands.open(WindowId::RadialProfile);
    break;
  }
  case ANALYSIS_2D_HISTOGRAM: {
    if (ImGui::Button("Compute 2D histogram"))
      windowCommands.open(WindowId::Histogram2D);
    break;
  }
  case ANALYSIS_CLUMP_FIND: {
    if (ImGui::Button("Run Clumps finder")) 
      windowCommands.open(WindowId::ClumpFinder);
    
#ifdef CLUMP_DATA_READ
    auto& batchReq = edit.clumpBatch;
    bool clumpBatchDirty = false;
    const auto& batchJob = jobs.clumpBatch.job;
    const auto* batchRes = result.clumpBatch;

    ImGui::Text("create clump data for continuous snapshots");

    clumpBatchDirty |= ImGui::RadioButton("FOF",        &batchReq.method, 0);
    ImGui::SameLine();
    clumpBatchDirty |= ImGui::RadioButton("Dendrogram", &batchReq.method, 1);

    clumpBatchDirty |= ImGui::InputInt("number of snapshots##FOF",
                                       &batchReq.nSnapshots);
    clumpBatchDirty |= ImGui::InputText("Output File Name##FOF",
                                        batchReq.outputFileName,
                                        IM_ARRAYSIZE(batchReq.outputFileName));
    clumpBatchDirty |= ImGui::InputText("Output Folder##FOF",
                                        batchReq.outputFolderPath,
                                        IM_ARRAYSIZE(batchReq.outputFolderPath));

    ImGui::SameLine();
    if (ImGui::Button("default path")) {
      std::strncpy(batchReq.outputFolderPath,
		   fileNav.input.folderPath,
		   IM_ARRAYSIZE(batchReq.outputFolderPath));
      batchReq.outputFolderPath[IM_ARRAYSIZE(batchReq.outputFolderPath) - 1] = '\0';
      clumpBatchDirty = true;
    }

    if (ImGui::Button("generate clump data")) {
      batchReq.generateClicked = true;
    }
    ImGui::SameLine();
    if (batchJob.status == JobStatus::Running) {
      if (ImGui::Button("cancel clump batch")) {
        batchReq.cancelClicked = true;
      }
      ImGui::Text("running %d / %d", batchJob.processed, batchReq.nSnapshots);
    }

    if (batchRes && batchRes->completed) {
      ImGui::Text("Processed snapshots: %d", batchRes->processedSnapshots);
      ImGui::Text("Output: %s", batchRes->outputPath);
    }

    if (batchRes && batchRes->errorMessage[0] != '\0') {
      ImGui::TextColored(ImVec4(1,0,0,1), "%s", batchRes->errorMessage);
    }

    if (ImGui::Button("show clump list")) {
      windowCommands.open(WindowId::ClumpList);
    }

    if (ImGui::Button("show clump chain list")) {
      windowCommands.open(WindowId::ClumpChain);
    }
    if (clumpBatchDirty) {
      edit.clumpBatchDirty = true;
    }
#endif
    break;
  }
  case ANALYSIS_STELLAR_DENSITY: {
    auto& req = edit.stellarDensity;
    bool stellarDensityDirty = false;

    ImGui::Text("Particle types to include:");
    stellarDensityDirty |= ImGui::Checkbox("Type 0##stellar_density", &req.selectedTypes[0]); ImGui::SameLine();
    stellarDensityDirty |= ImGui::Checkbox("Type 1##stellar_density", &req.selectedTypes[1]); ImGui::SameLine();
    stellarDensityDirty |= ImGui::Checkbox("Type 2##stellar_density", &req.selectedTypes[2]);
    stellarDensityDirty |= ImGui::Checkbox("Type 3##stellar_density", &req.selectedTypes[3]); ImGui::SameLine();
    stellarDensityDirty |= ImGui::Checkbox("Type 4##stellar_density", &req.selectedTypes[4]); ImGui::SameLine();
    stellarDensityDirty |= ImGui::Checkbox("Type 5##stellar_density", &req.selectedTypes[5]);

    stellarDensityDirty |= ImGui::Checkbox("overwrite hsml##stellar_density",
                                           &req.overwriteHsml);

    if (ImGui::Button("Select 3,4,5##stellar_density")) {
      for (int t = 0; t < 6; ++t) req.selectedTypes[t] = false;
      req.selectedTypes[3] = true;
      req.selectedTypes[4] = true;
      req.selectedTypes[5] = true;
      stellarDensityDirty = true;
    }

    if (ImGui::Button("Compute stellar density##stellar_density")) {
      req.computeClicked = true;
    }
    if (stellarDensityDirty) {
      edit.stellarDensityDirty = true;
    }

    break;
  }
#ifdef HAVE_HDF5
  case ANALYSIS_HALO_CATALOGUE: {
    if(ImGui::Button("Load Halo"))
      windowCommands.open(WindowId::Haloes);
    break;
  }
#endif
			
#ifdef POWER_SPECTRUM
  case ANALYSIS_POWER_SPEC: {
    break;
  }
#endif
    
#ifdef GEOMETRICAL_ANALYSIS
  case ANALYSIS_DISK: {
    auto& singleReq = edit.disk;
    const auto* singleRes = result.disk;
    
    ImGui::SeparatorText("Single disk analysis");

    if (ImGui::InputInt("Particle ID1##disk", &singleReq.targetParticleId)) {
      edit.diskDirty = true;
    }
    if (ImGui::SliderFloat("Opacity##disk",
                           &settingsReq.renderDraft.diskOpacity,
                           0.0f,
                           1.0f)) {
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
    }

    if (ImGui::Button("Find a disk around the particle")) {
      singleReq.findClicked = true;
    }

    if (ImGui::Button("disable disks")) {
      singleReq.clearClicked = true;
    }

    if (singleRes && singleRes->valid) {
      ImGui::Text("Disk radius: %g", singleRes->radius);
    }

    auto& batchReq  = edit.diskBatch;
    bool diskBatchDirty = false;
    const auto& batchJob = jobs.diskBatch.job;
    const auto& batchRuntime = jobs.diskBatch;
    const auto* batchRes  = result.diskBatch;    
    ImGui::SeparatorText("Batch disk analysis");

    diskBatchDirty |= ImGui::InputText("Read target from text file##disk",
                                       batchReq.inputFile,
                                       IM_ARRAYSIZE(batchReq.inputFile));
    diskBatchDirty |= ImGui::InputText("Output target from text file##disk",
                                       batchReq.outputFile,
                                       IM_ARRAYSIZE(batchReq.outputFile));

    if (ImGui::Button("calc disk radius from text file")) {
      batchReq.runClicked = true;
    }
    ImGui::SameLine();
    if (batchJob.status == JobStatus::Running) {
      if (ImGui::Button("cancel disk batch")) {
        batchReq.cancelClicked = true;
      }
      ImGui::Text("running %d / %d",
                  batchRuntime.rowCursor,
                  static_cast<int>(batchRuntime.rows.size()));
    }

    if (batchRes && batchRes->completed) {
      ImGui::Text("Processed rows: %d", batchRes->processedRows);
    }
    if (diskBatchDirty) {
      edit.diskBatchDirty = true;
    }

    break;
  }

  case ANALYSIS_ISO_DENSITY: {
    auto& singleReq = edit.ellipsoid;
    const auto* singleRes = result.ellipsoid;
    auto& batchReq  = edit.ellipsoidBatch;
    bool ellipsoidBatchDirty = false;
    const auto& batchJob = jobs.ellipsoidBatch.job;
    const auto& batchRuntime = jobs.ellipsoidBatch;
    const auto* batchRes  = result.ellipsoidBatch;

    ImGui::SeparatorText("Single ellipsoid analysis");

    if (ImGui::InputInt("Particle ID1", &singleReq.particleId1)) {
      edit.ellipsoidDirty = true;
    }
    if (ImGui::InputInt("Particle ID2", &singleReq.particleId2)) {
      edit.ellipsoidDirty = true;
    }
    if (ImGui::SliderFloat("Opacity##contour_ellipse",
                           &settingsReq.renderDraft.ellipsoidOpacity,
                           0.0f,
                           1.0f)) {
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
    }

    if (ImGui::Button("Fit Iso-density ellipsoid")) {
      singleReq.fitClicked = true;
    }

    if (ImGui::Button("disable Ellipsoid")) {
      singleReq.clearClicked = true;
    }

    if (singleRes && singleRes->valid) {
      ImGui::Text("a=%g b=%g c=%g",
		  singleRes->ellipsoid.radii.x,
		  singleRes->ellipsoid.radii.y,
		  singleRes->ellipsoid.radii.z);
    }

    ImGui::SeparatorText("Batch ellipsoid analysis");

    ellipsoidBatchDirty |= ImGui::InputText("Read target from text file",
                                            batchReq.inputFile,
                                            IM_ARRAYSIZE(batchReq.inputFile));
    ellipsoidBatchDirty |= ImGui::InputText("Output target from text file",
                                            batchReq.outputFile,
                                            IM_ARRAYSIZE(batchReq.outputFile));

    if (ImGui::Button("ellipsoidal fit from text file")) {
      batchReq.runClicked = true;
    }
    ImGui::SameLine();
    if (batchJob.status == JobStatus::Running) {
      if (ImGui::Button("cancel ellipsoid batch")) {
        batchReq.cancelClicked = true;
      }
      ImGui::Text("running %d / %d",
                  batchRuntime.rowCursor,
                  static_cast<int>(batchRuntime.rows.size()));
    }

    if (batchRes && batchRes->completed) {
      ImGui::Text("Processed rows: %d", batchRes->processedRows);
    }
    if (ellipsoidBatchDirty) {
      edit.ellipsoidBatchDirty = true;
    }

    break;
  }			
#endif      
  }
}


static void DrawRenderingSection(const QuantityState& quantity,
				 SettingsAnalysisEditState& edit,
                                 const AnalysisJobRuntimeState& jobs,
                                 const SettingsAnalysisResultView& result,
                                 SettingsUIState& ui,
                                 WindowCommandQueue& windowCommands,
				 SettingsActionRequestState& settingsReq){
  if (!ImGui::CollapsingHeader("Rendering"))
    return;

  const QuantityCatalogState& catalog = quantity.catalog;

  enum RenderingMode {
    RENDER_PROJECTION_MAP,
    RENDER_STREAM_LINE,
    RENDER_ISO_CONTOUR,
    RENDER_VOLUME,
    RENDER_VELOCITY_FIELD
  };
		
  static PullDownItem renderingItems[] = {
    { "projection map", RENDER_PROJECTION_MAP },
#ifdef STREAM_LINE
    { "stream line", RENDER_STREAM_LINE },
#endif
#ifdef ISO_CONTOUR
    { "iso-contour", RENDER_ISO_CONTOUR },
#endif
#ifdef VOLUME_RENDERING
    { "adaptive volume", RENDER_VOLUME },
#endif
    { "velocity field", RENDER_VELOCITY_FIELD},
  };
		
  // Find the currently selected label.
  const char* currentLabel = "unknown";
  for (const auto& item : renderingItems) {
    if (item.mode == ui.renderingMode) {
      currentLabel = item.label;
      break;
    }
  }
		
  if (ImGui::BeginCombo("Rendering mode", currentLabel)) {
    for (const auto& item : renderingItems) {
      bool isSelected = (ui.renderingMode == item.mode);
      if (ImGui::Selectable(item.label, isSelected)) {
	ui.renderingMode = item.mode;
      }
      if (isSelected)
	ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  auto& scheduling = settingsReq.renderDraft.scheduling;
  bool schedulingDirty = false;
  ImGui::SeparatorText("Interaction responsiveness");
  schedulingDirty |= ImGui::Checkbox("Responsive interaction",
                                     &scheduling.responsiveInteraction);
#ifdef VOLUME_RENDERING
  schedulingDirty |= ImGui::Checkbox("Hide volume while interacting",
                                     &scheduling.skipVolumeWhileInteracting);
#endif
  schedulingDirty |= ImGui::InputFloat("Interaction settle delay [s]",
                                       &scheduling.settleDelaySeconds,
                                       0.01f,
                                       0.05f,
                                       "%.3f");
  if (scheduling.settleDelaySeconds < 0.0f) {
    scheduling.settleDelaySeconds = 0.0f;
    schedulingDirty = true;
  }
  if (schedulingDirty) {
    settingsReq.renderDraftDirty = true;
    settingsReq.applyRenderRequested = true;
  }
		
  switch (ui.renderingMode) {
  case RENDER_PROJECTION_MAP: {
    if (ImGui::Button("make projection map"))
      windowCommands.open(WindowId::ProjectionMap);
	    
    auto& movieReq = edit.projectionMovie;
    bool movieDirty = false;
    const auto& movieJob = jobs.projectionMovie.job;
    const auto* movieRes = result.projectionMovie;

    ImGui::Text("create projection maps for continuous snapshots");

    if (ImGui::InputInt("number of snapshots##render", &movieReq.nSnapshots)) {
      movieDirty = true;
    }
    if (movieReq.nSnapshots < 1) {
      movieReq.nSnapshots = 1;
      movieDirty = true;
    }
    movieDirty |= ImGui::InputText("Output File Format##render",
		                   movieReq.outputFileFormat,
		                   IM_ARRAYSIZE(movieReq.outputFileFormat));
    movieDirty |= ImGui::InputText("Output Folder##render",
		                   movieReq.outputFolderPath,
		                   IM_ARRAYSIZE(movieReq.outputFolderPath));
    movieDirty |= ImGui::InputText("Output Name of Movie##render",
		                   movieReq.outputMovieName,
		                   IM_ARRAYSIZE(movieReq.outputMovieName));

    movieDirty |= ImGui::Checkbox("restore camera after movie", &movieReq.restoreCameraOnFinish);
    ImGui::SeparatorText("Movie Tracking");
    movieDirty |= ImGui::Checkbox("follow the center around the sink particle", &movieReq.followSinkCenter);
    if (movieReq.followSinkCenter) {
      movieDirty |= ImGui::Checkbox("the most massive sink particle", &movieReq.followMostMassiveSink);
      if (!movieReq.followMostMassiveSink) {
	movieDirty |= ImGui::InputInt("particle ID", &movieReq.particleIdCenter);
      }

      movieDirty |= ImGui::Checkbox("mass center around the particle", &movieReq.useMassCenter);
      if (movieReq.useMassCenter) {
	movieDirty |= ImGui::InputFloat("distance from the particle", &movieReq.massCenterRadius);
	movieDirty |= ImGui::InputFloat("the minimum density", &movieReq.massCenterMinDensity);
      }
    }

    ImGui::SeparatorText("Angular Momentum");
    movieDirty |= ImGui::Checkbox("force face-on view", &movieReq.faceOn);
    movieDirty |= ImGui::Checkbox("align camera to angular momentum", &movieReq.alignToAngularMomentum);

    const bool useAm = (movieReq.faceOn || movieReq.alignToAngularMomentum);
    if (useAm) {
      const char* amModes[] = {"Face-on", "Edge-on"};
      int amMode = movieReq.faceOn ? 0 : static_cast<int>(movieReq.amViewMode);
      if (ImGui::Combo("AM view mode", &amMode, amModes, IM_ARRAYSIZE(amModes)) && !movieReq.faceOn) {
        movieReq.amViewMode = static_cast<AngularMomentumViewMode>(amMode);
        movieDirty = true;
      }
      movieDirty |= ImGui::InputFloat("AM radius", &movieReq.amRadius, 0.f, 0.f, "%g");
      movieDirty |= ImGui::Checkbox("Subtract bulk velocity", &movieReq.amSubtractBulkVelocity);
      movieDirty |= ImGui::Checkbox("Keep axis sign continuity", &movieReq.amKeepSignContinuity);
      for (int t = 0; t < 6; ++t) {
        char label[64];
        std::snprintf(label, sizeof(label), "use type %d##movie_am_type", t);
        movieDirty |= ImGui::Checkbox(label, &movieReq.amUseType[t]);
        if (t < 5) ImGui::SameLine();
      }
    }

    if (movieJob.status == JobStatus::Running) {
      if (ImGui::Button("cancel movie")) {
        movieReq.cancelClicked = true;
      }
      ImGui::SameLine();
      ImGui::Text("running %d / %d", movieJob.processed, movieReq.nSnapshots);
    } else if (ImGui::Button("generate maps")) {
      movieReq.cancelClicked = false;
      movieReq.generateClicked = true;
      edit.projectionMovieDirty = true;
    }

    if (movieDirty) {
      edit.projectionMovieDirty = true;
    }

    if (movieRes && movieRes->completed) {
      ImGui::Text("Processed snapshots: %d", movieRes->processedSnapshots);
      ImGui::Text("Movie: %s", movieRes->outputMoviePath);
    }

    if (movieRes && movieRes->errorMessage[0] != '\0') {
      ImGui::TextColored(ImVec4(1,0,0,1), "%s", movieRes->errorMessage);
    }
    
    break;
  }
			
#ifdef STREAM_LINE
  case RENDER_STREAM_LINE: {
    auto& previewReq = edit.streamlinePreview;
    auto& buildReq   = edit.streamlineBuild;
    bool buildDirty = false;

    ImGui::Text("Seed setup");
    const char* fieldSources[] = {"velocity", "B field"};
    if (ImGui::Combo("vector field",
                     &buildReq.fieldSource,
                     fieldSources,
                     IM_ARRAYSIZE(fieldSources))) {
      buildDirty = true;
      buildReq.buildClicked = true;
    }
    buildDirty |= ImGui::InputInt("number of seed points", &buildReq.nSeeds);
    buildDirty |= ImGui::InputInt("max integration steps", &buildReq.maxSteps);
    buildDirty |= ImGui::InputFloat("step scale [hsml]",
                                    &buildReq.stepScale, 0.01f, 0.05f, "%.4f");
    buildDirty |= ImGui::InputFloat("curvature angle threshold [deg]",
                                    &buildReq.thetaMaxDegrees);
    if (ImGui::Checkbox("use manual seed", &buildReq.useManualSeed)) {
      buildDirty = true;
      buildReq.buildClicked = true;
    }

    if (buildReq.useManualSeed) {
      if (buildReq.manualSeeds.empty()) {
        buildReq.manualSeeds.push_back({0.f, 0.f, 0.f});
      }
      int removeSeed = -1;
      for (int i = 0; i < static_cast<int>(buildReq.manualSeeds.size()); ++i) {
        ImGui::PushID(i);
        if (ImGui::InputFloat3("manual seed position",
                               buildReq.manualSeeds[i].data(), "%.3f")) {
          buildDirty = true;
          buildReq.buildClicked = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("remove")) {
          removeSeed = i;
        }
        ImGui::PopID();
      }
      if (removeSeed >= 0 &&
          buildReq.manualSeeds.size() > 1) {
        buildReq.manualSeeds.erase(buildReq.manualSeeds.begin() + removeSeed);
        buildDirty = true;
        buildReq.buildClicked = true;
      }
      if (ImGui::Button("Add manual seed")) {
        buildReq.manualSeeds.push_back(buildReq.manualSeeds.back());
        buildDirty = true;
        buildReq.buildClicked = true;
      }
    }

    bool previewDirty = false;

    if (ImGui::InputFloat3("Center of the region to place seed points",
			   previewReq.seedCenter, "%.3f")) {
      previewDirty = true;
    }

    if (ImGui::InputFloat3("side len",
			   previewReq.seedSize, "%.3f")) {
      previewDirty = true;
    }

    if (ImGui::SliderFloat("opacity##cubic",
			   &previewReq.opacity, 0.f, 1.f, "%.2f")) {
      previewDirty = true;
    }

    if (previewDirty) {
      previewReq.updateClicked = true;
      edit.streamlinePreviewDirty = true;
    }

    ImGui::Text("Stream line setting");

    if (ImGui::Checkbox("limit stream lines in box", &buildReq.limitRegion)) {
      buildDirty = true;
    }

    if (buildReq.limitRegion) {
      buildDirty |= ImGui::InputFloat3("center of stream line region",
                                       buildReq.regionCenter, "%.3f");
      buildDirty |= ImGui::InputFloat3("side len##stream line",
                                       buildReq.regionSize, "%.3f");
    }

    if (result.streamlineBuild &&
        !result.streamlineBuild->message.empty()) {
      const ImVec4 color = result.streamlineBuild->success
        ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f)
        : ImVec4(1.0f, 0.45f, 0.25f, 1.0f);
      ImGui::TextColored(color, "%s",
                         result.streamlineBuild->message.c_str());
      ImGui::Text("lines: %d / seeds: %d",
                  result.streamlineBuild->lineCount,
                  result.streamlineBuild->seedCount);
      static const char* stopLabels[] = {
        "none",
        "seed outside",
        "field eval failed",
        "weak field",
        "out of bounds",
        "zero step",
        "max steps"
      };
      for (int i = 0; i < 7; ++i) {
        const int count = result.streamlineBuild->stopCounts[i];
        if (count > 0) {
          ImGui::Text("  %s: %d", stopLabels[i], count);
        }
      }
      if (!result.streamlineBuild->seedReports.empty() &&
          ImGui::TreeNode("Seed details")) {
        for (const auto& seed : result.streamlineBuild->seedReports) {
          const int reason = (seed.stopReason >= 0 && seed.stopReason < 7)
            ? seed.stopReason
            : 0;
          ImGui::Text("#%d stop=%s points=%d length=%.6g pos=(%.3f, %.3f, %.3f)",
                      seed.seedIndex,
                      stopLabels[reason],
                      seed.pointCount,
                      seed.length,
                      seed.position[0],
                      seed.position[1],
                      seed.position[2]);
        }
        ImGui::TreePop();
      }
    }

    if (ImGui::Button("Build stream lines")) {
      buildReq.buildClicked = true;
    }

    if (ImGui::Button("disable Grid & Mesh")) {
      buildReq.clearClicked = true;
    }
    if (buildDirty) {
      edit.streamlineBuildDirty = true;
    }

    break;
  }    
#endif

#ifdef ISO_CONTOUR
  case RENDER_ISO_CONTOUR: {
    auto& req = edit.isoContour;
    bool isoContourDirty = false;

    isoContourDirty |= ImGui::InputFloat("Threshold value for iso-contour",
                                         &req.isoLevel);
    if (ImGui::SliderFloat("Opacity",
                           &settingsReq.renderDraft.isoContourOpacity,
                           0.0f,
                           1.0f)) {
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
    }
    isoContourDirty |= ImGui::SliderInt("Maximum level of spatial tree",
                                        &req.maxTreeLevel,
                                        5,
                                        20);

    if (ImGui::BeginCombo("Quantity for Iso-Contour",
			  QuantityLabel(req.selectedQuantity))) {
      for (int q = 0; q < catalog.nUIQ; ++q) {
	QuantityId cand = catalog.uiQ[q];
	bool is_selected = (cand == req.selectedQuantity);
	if (ImGui::Selectable(QuantityLabel(cand), is_selected)) {
	  req.selectedQuantity = cand;
          isoContourDirty = true;
	}
	if (is_selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    if (ImGui::Button("Build spatial tree & mesh")) {
      req.buildClicked = true;
    }

    if (ImGui::Button("disable Grid & Mesh")) {
      req.clearClicked = true;
    }
    if (isoContourDirty) {
      edit.isoContourDirty = true;
    }

    break;
  }
#endif

#ifdef VOLUME_RENDERING
  case RENDER_VOLUME: {
    DrawVolumeRenderingSettingsSection(quantity, edit, result, settingsReq);
    break;
  }
#endif
			
	  case RENDER_VELOCITY_FIELD: {
    auto& velocity = settingsReq.renderDraft.velocity;
    if (ImGui::InputInt("show velocity field out of n particles", &velocity.subtraction)) {
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
      settingsReq.velocityRenderDirtyRequested = true;
    }
    if (ImGui::InputFloat("Arrow Scale", &velocity.arrowScale, 0.1f, 1.0f, "%.2f")) {
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
    }
    if (ImGui::Checkbox("Use Log Scale", &velocity.useLogScale)) {
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
    }
					
    if (ImGui::Checkbox("render velocity field", &velocity.show)) {
      settingsReq.renderDraftDirty = true;
      settingsReq.applyRenderRequested = true;
      settingsReq.velocityRenderDirtyRequested = true;
    }
    break;
  }  
  }
}


static void DrawOtherSettingsSection(SettingsRuntimeState& rt)
{
  if (!ImGui::CollapsingHeader("Other settings"))
    return;

  auto& req = rt.request;
  bool unitChanged = false;
  auto& units = req.unitsDraft;
  if (ImGui::CollapsingHeader("Units")) {
    unitChanged |= ImGui::InputDouble("UnitLength_in_cm",
                                      &units.length_cm,
                                      0., 0., "%g");
    unitChanged |= ImGui::InputDouble("UnitMass_in_g",
                                      &units.mass_g,
                                      0., 0., "%g");
    unitChanged |= ImGui::InputDouble("UnitVelocity_in_cm_per_s",
                                      &units.velocity_cm_per_s,
                                      0., 0., "%g");
    unitChanged |= ImGui::InputDouble("Hubble",
                                      &units.hubble,
                                      0., 0., "%g");
    unitChanged |= ImGui::Checkbox("ComovingCoordinate",
                                   &units.useComovingCoordinate);

    ImGui::SeparatorText("Presets");

    if (ImGui::Button("AU"))   { units.setLengthToAU();      unitChanged = true; }
    ImGui::SameLine();
    if (ImGui::Button("pc"))   { units.setLengthToPC();      unitChanged = true; }
    ImGui::SameLine();
    if (ImGui::Button("kpc"))  { units.setLengthToKPC();     unitChanged = true; }
    ImGui::SameLine();
    if (ImGui::Button("Mpc"))  { units.setLengthToMPC();     unitChanged = true; }

    if (ImGui::Button("Msun")) {
      units.setMassToSolar();
      unitChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("1e10 Msun")) {
      units.setMassTo1e10Solar();
      unitChanged = true;
    }
  }

  if (unitChanged) {
    req.unitsDraftDirty = true;
    req.applyUnitsRequested = true;
    req.unitConversionRebuildRequested = true;
  }

  if (ImGui::CollapsingHeader("Zoom Range")) {
    ImGui::InputFloat("Min Zoom", &rt.minZoom, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Max Zoom", &rt.maxZoom, 0.0f, 0.0f, "%g");
  }

  if (ImGui::CollapsingHeader("Cross Marker")) {
    if (ImGui::SliderFloat("Cross Marker Size",
                           &req.renderDraft.crossGizmoSize,
                           0.01f,
                           1.0f)) {
      req.renderDraftDirty = true;
      req.applyRenderRequested = true;
    }
  }
}
