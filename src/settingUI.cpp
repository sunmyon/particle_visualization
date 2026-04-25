#include "UI.h"
#include "settingUI.h"
#include "app/app_state.h"
#include "app/snapshot_state_sync.h"
#include "render_actions.h"

#include "interaction/camera.h"
#include "particle_visual_config.h"   // ParticleVisualConfig の実定義
#include "FileIO/file_format_dialog.h"
#include "render/colormap_defs.h"  
#include "data/particle_array.h"

#include "FindClumps/find_clumps_ui.h"
#include <imgui.h>

#ifndef NONATIVEFILEDIALOG
#include <nfd.h>
#else
#include "ImGuiFileDialog.h" // インクルードパスを合わせる
#endif


struct PullDownItem {
  const char* label;
  int mode;
};

static void DrawCameraInfoSection(const CameraContext& camCtx);
static void DrawParticleTypeSettingsSection(QuantityState& quantity,
					    ParticleVisualConfig& particleVisual,
					    SettingsActionRequestState& req);
static void DrawFileNavigationSection(FileNavigationRuntimeState& rt,
                                      SnapshotFormatState& format,
                                      bool isLoading,
                                      ToolWindowUIState& tools);
static void DrawNormalizationSection(NormalizationContext& ctx,
				     SettingsActionRequestState& req);
static void DrawSinkIdSection(const CameraContext& camCtx, ParticleLabelRenderState& labels);
static void DrawCameraPlacementSection(SettingsRuntimeState& rt, const CameraContext& camCtx);
#ifdef PYTHON_BRIDGE
static void DrawPythonBridgeSection(PythonBridgeRequestState& request, const PythonBridgeViewState& view);
#endif
static void DrawAnalysisSection(RenderRuntimeState& render,
				AnalysisDerivedState& analysis,
                                AnalysisRequestRuntimeState& rt,
                                const FileNavigationRuntimeState& fileNav,
                                ToolWindowUIState& tools);

static void DrawRenderingSection(RenderRuntimeState& render,
				 AnalysisDerivedState& analysis,
				 QuantityCatalogState& catalog,
				 AnalysisRequestRuntimeState& rt,
				 ToolWindowUIState& tools,
				 SettingsActionRequestState& settingsReq);

static void DrawOtherSettingsSection(UnitSystem& units, SettingsRuntimeState& rt, RenderRuntimeState& render);

void ShowSettingsUI(SettingsUIContext& ctx, AppRuntimeState& rt) {
  ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysVerticalScrollbar);

  DrawCameraInfoSection(*ctx.camCtx);
  DrawParticleTypeSettingsSection(*ctx.quantity, *ctx.particleVisual, rt.settings.request);
  DrawFileNavigationSection(rt.settings.fileNavigation,
                            rt.settings.snapshotFormat,
                            rt.snapshotLoad.busy,
                            *ctx.windows);
  DrawNormalizationSection(rt.settings.normalization, rt.settings.request);
  DrawSinkIdSection(*ctx.camCtx, ctx.render->particleLabels);
  DrawCameraPlacementSection(rt.settings, *ctx.camCtx);
#ifdef PYTHON_BRIDGE
  DrawPythonBridgeSection(rt.analysis.py.request, rt.analysis.py.view);
#endif
  DrawAnalysisSection(*ctx.render, *ctx.analysis, rt.analysis, rt.settings.fileNavigation, *ctx.windows);
  DrawRenderingSection(*ctx.render, *ctx.analysis, ctx.quantity->catalog, rt.analysis, *ctx.windows, rt.settings.request);
  DrawOtherSettingsSection(ctx.quantity->units, rt.settings, rt.render);

  ImGui::End();
}

static void DrawCameraInfoSection(const CameraContext& camCtx) {
  ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", camCtx.cameraPos.x, camCtx.cameraPos.y, camCtx.cameraPos.z);
  ImGui::Text("Camera Target:   (%.2f, %.2f, %.2f)", camCtx.cameraTarget.x, camCtx.cameraTarget.y, camCtx.cameraTarget.z);
}

static void DrawParticleTypeSettingsSection(QuantityState& quantity,
					    ParticleVisualConfig& particleVisual,
					    SettingsActionRequestState& req) {
  if (!ImGui::CollapsingHeader("Particle Type Settings"))
    return;
    
  for (int i = 0; i < 6; i++) {
    std::string header = "Type " + std::to_string(i);
    if (ImGui::TreeNode(header.c_str())) {
      auto& cfg = particleVisual.types[i];
      bool visualChanged = false;
				
      std::string comboLabel = "Colormap##" + std::to_string(i);
      const char* preview = gColormapDefs[cfg.colormapIndex].name;
      if (ImGui::BeginCombo(comboLabel.c_str(), preview)) {
	for (int k = 0; k < gNumColormaps; ++k) {
	  bool selected = (cfg.colormapIndex == k);
	  if (ImGui::Selectable(gColormapDefs[k].name, selected)) {
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
				
      auto findIndex = [&](QuantityId q)->int{
	for (int k = 0; k < quantity.catalog.nUIQ; ++k) if (quantity.catalog.uiQ[k] == q) return k;
	return 0; // fallback
      };
				
      int qidx = findIndex(sel);
      ImGui::Text("Current particle %s range: [%g, %g]",
		  QuantityLabel(sel),
		  quantity.range.valueMin[qidx][i],
		  quantity.range.valueMax[qidx][i]);
				
      if (visualChanged) {
	req.particleRenderDirtyRequested = true;
      }
				
      ImGui::TreePop();
    }
  }    
}

static void DrawFileNavigationSection(FileNavigationRuntimeState& rt,
                                      SnapshotFormatState& format,
                                      bool isLoading,
                                      ToolWindowUIState& tools){
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
    // 初期ディレクトリの設定（"path" メンバー）
    //config.path = src.filePath;
    config.path = input.folderPath;
    // 必要なら初期のファイル名を設定（空の場合はユーザー入力待ち）
    config.fileName = "output"; 
    // その他、選択可能なファイル数などの設定はデフォルトのままでOK
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
    if (input.useHDF5)
      OpenHDF5FormatDialog(tools.fileFormatDialog, format.formatTokensHdf5);
    else
#endif
      OpenBinaryFormatDialog(tools.fileFormatDialog, format.formatTokens);
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
    OpenMaskUI(tools.mask);
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

static void DrawSinkIdSection(const CameraContext& camCtx,
                              ParticleLabelRenderState& labels)
{
  if (!ImGui::CollapsingHeader("set sink ID visualization"))
    return;

  ImGui::InputFloat("radius", &labels.queryRadius, 0.f, 0.f, "%g");
  ImGui::InputInt("number of particles", &labels.maxLabels);

  labels.moveThreshold = 0.1f * labels.queryRadius;

  if (ImGui::Button("show sink IDs")) {
    labels.show = true;
    labels.lastCameraPos = camCtx.cameraPos;
  }

  if (ImGui::Button("disable sink IDs")) {
    labels.show = false;
  }
}

static void DrawCameraPlacementSection(SettingsRuntimeState& rt,
				       const CameraContext& camCtx)
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
    rt.viewFilter.center = camCtx.cameraTarget;
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
static void DrawPythonBridgeSection(PythonBridgeRequestState& request,
                             const PythonBridgeViewState& view)
{
  if (!ImGui::CollapsingHeader("Python:Jupyter notebook"))
    return;

  const bool isOpen = view.available;

  if (ImGui::Button(isOpen ? "Close notebook" : "Open notebook")) {
    if (isOpen)
      request.shutdownRequested = true;
    else
      request.launchRequested = true;
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
      request.openBrowserRequested = true;
    }

    if (!view.lastError.empty()) {
      ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "%s", view.lastError.c_str());
    }
  }
}
#endif

static void DrawAnalysisSection(RenderRuntimeState& render,
				AnalysisDerivedState& analysis,
                                AnalysisRequestRuntimeState& rt,
                                const FileNavigationRuntimeState& fileNav,
                                ToolWindowUIState& tools){
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
		
  static int analysisMode = ANALYSIS_RADIAL_PROFILE;
		
  // 現在選択中のラベルを探す
  const char* currentLabel = "unknown";
  for (const auto& item : analysisItems) {
    if (item.mode == analysisMode) {
      currentLabel = item.label;
      break;
    }
  }
		
  if (ImGui::BeginCombo("Analysis mode", currentLabel)) {
    for (const auto& item : analysisItems) {
      bool isSelected = (analysisMode == item.mode);
      if (ImGui::Selectable(item.label, isSelected)) {
	analysisMode = item.mode;
      }
      if (isSelected)
	ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
		
  switch (analysisMode) {  
  case ANALYSIS_RADIAL_PROFILE: {
    if (ImGui::Button("Compute radial profile"))
      OpenRadialProfileUI(tools.radialProfile);
    break;
  }
  case ANALYSIS_2D_HISTOGRAM: {
    if (ImGui::Button("Compute 2D histogram"))
      OpenHistogram2DUI(tools.histogram2D);
    break;
  }
  case ANALYSIS_CLUMP_FIND: {
    if (ImGui::Button("Run Clumps finder")) 
      OpenClumpFindUI(tools.clumpFind);
    
#ifdef CLUMP_DATA_READ
    auto& batchReq = rt.clumpBatch;
    auto& batchRes = analysis.clumpBatch;

    ImGui::Text("create clump data for continuous snapshots");

    ImGui::RadioButton("FOF",        &batchReq.method, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Dendrogram", &batchReq.method, 1);

    ImGui::InputInt("number of snapshots##FOF", &batchReq.nSnapshots);
    ImGui::InputText("Output File Name##FOF",
		     batchReq.outputFileName,
		     IM_ARRAYSIZE(batchReq.outputFileName));
    ImGui::InputText("Output Folder##FOF",
		     batchReq.outputFolderPath,
		     IM_ARRAYSIZE(batchReq.outputFolderPath));

    ImGui::SameLine();
    if (ImGui::Button("default path")) {
      std::strncpy(batchReq.outputFolderPath,
		   fileNav.input.folderPath,
		   IM_ARRAYSIZE(batchReq.outputFolderPath));
      batchReq.outputFolderPath[IM_ARRAYSIZE(batchReq.outputFolderPath) - 1] = '\0';
    }

    if (ImGui::Button("generate clump data")) {
      batchReq.runRequested = true;
    }
    ImGui::SameLine();
    if (batchReq.job.status == JobStatus::Running) {
      if (ImGui::Button("cancel clump batch")) {
        batchReq.cancelRequested = true;
      }
      ImGui::Text("running %d / %d", batchReq.job.processed, batchReq.nSnapshots);
    }

    if (batchRes.completed) {
      ImGui::Text("Processed snapshots: %d", batchRes.processedSnapshots);
      ImGui::Text("Output: %s", batchRes.outputPath);
    }

    if (batchRes.errorMessage[0] != '\0') {
      ImGui::TextColored(ImVec4(1,0,0,1), "%s", batchRes.errorMessage);
    }

    if (ImGui::Button("show clump list")) {
      OpenClumpListUI(tools.clumpList);
    }

    if (ImGui::Button("show clump chain list")) {
      OpenClumpChainUI(tools.clumpChain);
    }
#endif
    break;
  }
  case ANALYSIS_STELLAR_DENSITY: {
    auto& req = rt.stellarDensity;

    ImGui::Text("Particle types to include:");
    ImGui::Checkbox("Type 0##stellar_density", &req.selectedTypes[0]); ImGui::SameLine();
    ImGui::Checkbox("Type 1##stellar_density", &req.selectedTypes[1]); ImGui::SameLine();
    ImGui::Checkbox("Type 2##stellar_density", &req.selectedTypes[2]);
    ImGui::Checkbox("Type 3##stellar_density", &req.selectedTypes[3]); ImGui::SameLine();
    ImGui::Checkbox("Type 4##stellar_density", &req.selectedTypes[4]); ImGui::SameLine();
    ImGui::Checkbox("Type 5##stellar_density", &req.selectedTypes[5]);

    ImGui::Checkbox("overwrite hsml##stellar_density", &req.overwriteHsml);

    if (ImGui::Button("Select 3,4,5##stellar_density")) {
      for (int t = 0; t < 6; ++t) req.selectedTypes[t] = false;
      req.selectedTypes[3] = true;
      req.selectedTypes[4] = true;
      req.selectedTypes[5] = true;
    }

    if (ImGui::Button("Compute stellar density##stellar_density")) {
      req.runRequested = true;
    }

    break;
  }
#ifdef HAVE_HDF5
  case ANALYSIS_HALO_CATALOGUE: {
    if(ImGui::Button("Load Halo"))
      OpenHaloesUI(tools.haloes);
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
    auto& singleReq = rt.disk;
    auto& singleRes = analysis.disk;
    
    ImGui::SeparatorText("Single disk analysis");

    ImGui::InputInt("Particle ID1##disk", &singleReq.targetParticleId);
    ImGui::SliderFloat("Opacity##disk", &render.disks.opacity, 0.0f, 1.0f);

    if (ImGui::Button("Find a disk around the particle")) {
      singleReq.runRequested = true;
    }

    if (ImGui::Button("disable disks")) {
      singleReq.clearRequested = true;
    }

    if (singleRes.valid) {
      ImGui::Text("Disk radius: %g", singleRes.radius);
    }

    auto& batchReq  = rt.diskBatch;
    auto& batchRes  = analysis.diskBatch;    
    ImGui::SeparatorText("Batch disk analysis");

    ImGui::InputText("Read target from text file##disk",
		     batchReq.inputFile,
		     IM_ARRAYSIZE(batchReq.inputFile));
    ImGui::InputText("Output target from text file##disk",
		     batchReq.outputFile,
		     IM_ARRAYSIZE(batchReq.outputFile));

    if (ImGui::Button("calc disk radius from text file")) {
      batchReq.runRequested = true;
    }
    ImGui::SameLine();
    if (batchReq.job.status == JobStatus::Running) {
      if (ImGui::Button("cancel disk batch")) {
        batchReq.cancelRequested = true;
      }
      ImGui::Text("running %d / %d", batchReq.rowCursor, static_cast<int>(batchReq.rows.size()));
    }

    if (batchRes.completed) {
      ImGui::Text("Processed rows: %d", batchRes.processedRows);
    }

    break;
  }

  case ANALYSIS_ISO_DENSITY: {
    auto& singleReq = rt.ellipsoid;
    auto& singleRes = analysis.ellipsoid;
    auto& batchReq  = rt.ellipsoidBatch;
    auto& batchRes  = analysis.ellipsoidBatch;

    ImGui::SeparatorText("Single ellipsoid analysis");

    ImGui::InputInt("Particle ID1", &singleReq.particleId1);
    ImGui::InputInt("Particle ID2", &singleReq.particleId2);
    ImGui::SliderFloat("Opacity##contour_ellipse", &render.ellipsoids.opacity, 0.0f, 1.0f);

    if (ImGui::Button("Fit Iso-density ellipsoid")) {
      singleReq.runRequested = true;
    }

    if (ImGui::Button("disable Ellipsoid")) {
      singleReq.clearRequested = true;
    }

    if (singleRes.valid) {
      ImGui::Text("a=%g b=%g c=%g",
		  singleRes.ellipsoid.radii.x,
		  singleRes.ellipsoid.radii.y,
		  singleRes.ellipsoid.radii.z);
    }

    ImGui::SeparatorText("Batch ellipsoid analysis");

    ImGui::InputText("Read target from text file",
		     batchReq.inputFile,
		     IM_ARRAYSIZE(batchReq.inputFile));
    ImGui::InputText("Output target from text file",
		     batchReq.outputFile,
		     IM_ARRAYSIZE(batchReq.outputFile));

    if (ImGui::Button("ellipsoidal fit from text file")) {
      batchReq.runRequested = true;
    }
    ImGui::SameLine();
    if (batchReq.job.status == JobStatus::Running) {
      if (ImGui::Button("cancel ellipsoid batch")) {
        batchReq.cancelRequested = true;
      }
      ImGui::Text("running %d / %d", batchReq.rowCursor, static_cast<int>(batchReq.rows.size()));
    }

    if (batchRes.completed) {
      ImGui::Text("Processed rows: %d", batchRes.processedRows);
    }

    break;
  }			
#endif      
  }
}


static void DrawRenderingSection(RenderRuntimeState& render,
				 AnalysisDerivedState& analysis,
				 QuantityCatalogState& catalog,
				 AnalysisRequestRuntimeState& rt,
				 ToolWindowUIState& tools,
				 SettingsActionRequestState& settingsReq){
  if (!ImGui::CollapsingHeader("Rendering"))
    return;
  
  enum RenderingMode {
    RENDER_PROJECTION_MAP,
    RENDER_STREAM_LINE,
    RENDER_ISO_CONTOUR,
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
    { "velocity field", RENDER_VELOCITY_FIELD},
  };
		
  static int renderingMode = RENDER_PROJECTION_MAP;
		
  // 現在選択中のラベルを探す
  const char* currentLabel = "unknown";
  for (const auto& item : renderingItems) {
    if (item.mode == renderingMode) {
      currentLabel = item.label;
      break;
    }
  }
		
  if (ImGui::BeginCombo("Rendering mode", currentLabel)) {
    for (const auto& item : renderingItems) {
      bool isSelected = (renderingMode == item.mode);
      if (ImGui::Selectable(item.label, isSelected)) {
	renderingMode = item.mode;
      }
      if (isSelected)
	ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
		
  switch (renderingMode) {
  case RENDER_PROJECTION_MAP: {
    if (ImGui::Button("make projection map"))
      OpenProjectionMapUI(tools.projectionMap);    
    
    auto& movieRt = rt.projectionMovie;
    auto& movieReq = movieRt.request;
    auto& movieJob = movieRt.job;
    auto& movieRes = analysis.projectionMovie;

    ImGui::Text("create projection maps for continuous snapshots");

    ImGui::InputInt("number of snapshots##render", &movieReq.nSnapshots);
    if (movieReq.nSnapshots < 1) movieReq.nSnapshots = 1;
    ImGui::InputText("Output File Format##render",
		     movieReq.outputFileFormat,
		     IM_ARRAYSIZE(movieReq.outputFileFormat));
    ImGui::InputText("Output Folder##render",
		     movieReq.outputFolderPath,
		     IM_ARRAYSIZE(movieReq.outputFolderPath));
    ImGui::InputText("Output Name of Movie##render",
		     movieReq.outputMovieName,
		     IM_ARRAYSIZE(movieReq.outputMovieName));

    ImGui::Checkbox("restore camera after movie", &movieReq.restoreCameraOnFinish);
    ImGui::SeparatorText("Movie Tracking");
    ImGui::Checkbox("follow the center around the sink particle", &movieReq.followSinkCenter);
    if (movieReq.followSinkCenter) {
      ImGui::Checkbox("the most massive sink particle", &movieReq.followMostMassiveSink);
      if (!movieReq.followMostMassiveSink) {
	ImGui::InputInt("particle ID", &movieReq.particleIdCenter);
      }

      ImGui::Checkbox("mass center around the particle", &movieReq.useMassCenter);
      if (movieReq.useMassCenter) {
	ImGui::InputFloat("distance from the particle", &movieReq.massCenterRadius);
	ImGui::InputFloat("the minimum density", &movieReq.massCenterMinDensity);
      }
    }

    ImGui::SeparatorText("Angular Momentum");
    ImGui::Checkbox("force face-on view", &movieReq.faceOn);
    ImGui::Checkbox("align camera to angular momentum", &movieReq.alignToAngularMomentum);

    const bool useAm = (movieReq.faceOn || movieReq.alignToAngularMomentum);
    if (useAm) {
      const char* amModes[] = {"Face-on", "Edge-on"};
      int amMode = movieReq.faceOn ? 0 : static_cast<int>(movieReq.amViewMode);
      if (ImGui::Combo("AM view mode", &amMode, amModes, IM_ARRAYSIZE(amModes)) && !movieReq.faceOn) {
        movieReq.amViewMode = static_cast<AngularMomentumViewMode>(amMode);
      }
      ImGui::InputFloat("AM radius", &movieReq.amRadius, 0.f, 0.f, "%g");
      ImGui::Checkbox("Subtract bulk velocity", &movieReq.amSubtractBulkVelocity);
      ImGui::Checkbox("Keep axis sign continuity", &movieReq.amKeepSignContinuity);
      for (int t = 0; t < 6; ++t) {
        char label[64];
        std::snprintf(label, sizeof(label), "use type %d##movie_am_type", t);
        ImGui::Checkbox(label, &movieReq.amUseType[t]);
        if (t < 5) ImGui::SameLine();
      }
    }

    if (movieJob.status == JobStatus::Running) {
      if (ImGui::Button("cancel movie")) {
        movieReq.cancelRequested = true;
      }
      ImGui::SameLine();
      ImGui::Text("running %d / %d", movieJob.processed, movieReq.nSnapshots);
    } else if (ImGui::Button("generate maps")) {
      movieReq.cancelRequested = false;
      movieReq.runRequested = true;
    }

    if (movieRes.completed) {
      ImGui::Text("Processed snapshots: %d", movieRes.processedSnapshots);
      ImGui::Text("Movie: %s", movieRes.outputMoviePath);
    }

    if (movieRes.errorMessage[0] != '\0') {
      ImGui::TextColored(ImVec4(1,0,0,1), "%s", movieRes.errorMessage);
    }
    
    break;
  }
			
#ifdef STREAM_LINE
  case RENDER_STREAM_LINE: {
    auto& previewReq = rt.streamlinePreview;
    auto& buildReq   = rt.streamlineBuild;

    ImGui::Text("Seed setup");
    ImGui::InputInt("number of seed points", &buildReq.nSeeds);

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
      previewReq.updateRequested = true;
    }

    ImGui::Text("Stream line setting");

    if (ImGui::Checkbox("limit stream lines in box", &buildReq.limitRegion)) {
      //Just change the request state here.
    }

    if (buildReq.limitRegion) {
      ImGui::InputFloat3("center of stream line region",
			 buildReq.regionCenter, "%.3f");
      ImGui::InputFloat3("side len##stream line",
			 buildReq.regionSize, "%.3f");
    }

    if (ImGui::Button("Build stream lines")) {
      buildReq.runRequested = true;
    }

    if (ImGui::Button("disable Grid & Mesh")) {
      buildReq.clearRequested = true;
    }

    break;
  }    
#endif

#ifdef ISO_CONTOUR
  case RENDER_ISO_CONTOUR: {
    auto& req = rt.isoContour;

    ImGui::InputFloat("Threshold value for iso-contour", &req.isoLevel);
    ImGui::SliderFloat("Opacity", &render.isocontour.opacity, 0.0f, 1.0f);
    ImGui::SliderInt("Maximum level of OctTree", &req.maxTreeLevel, 5, 20);

    if (ImGui::BeginCombo("Quantity for Iso-Contour",
			  QuantityLabel(req.selectedQuantity))) {
      for (int q = 0; q < catalog.nUIQ; ++q) {
	QuantityId cand = catalog.uiQ[q];
	bool is_selected = (cand == req.selectedQuantity);
	if (ImGui::Selectable(QuantityLabel(cand), is_selected)) {
	  req.selectedQuantity = cand;
	}
	if (is_selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    if (ImGui::Button("Build OctTree & Mesh")) {
      req.runRequested = true;
    }

    if (ImGui::Button("disable Grid & Mesh")) {
      req.clearRequested = true;
    }

    break;
  }
#endif
			
  case RENDER_VELOCITY_FIELD: {
    if (ImGui::InputInt("show velocity field out of n particles", &render.velocity.subtraction)) {
      settingsReq.velocityRenderDirtyRequested = true;
    }
    ImGui::InputFloat("Arrow Scale", &render.velocity.arrowScale, 0.1f, 1.0f, "%.2f");
    ImGui::Checkbox("Use Log Scale", &render.velocity.useLogScale);
				
    if (ImGui::Checkbox("render velocity field", &render.velocity.show)) {
      settingsReq.velocityRenderDirtyRequested = true;
    }
    break;
  }  
  }
}


static void DrawOtherSettingsSection(UnitSystem& units, SettingsRuntimeState& rt, RenderRuntimeState& render)
{
  if (!ImGui::CollapsingHeader("Other settings"))
    return;

  bool unitChanged = false;
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
    units.updateDerived();
  }

  if (ImGui::CollapsingHeader("Zoom Range")) {
    ImGui::InputFloat("Min Zoom", &rt.minZoom, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Max Zoom", &rt.maxZoom, 0.0f, 0.0f, "%g");
  }

  if (ImGui::CollapsingHeader("Cross Marker")) {
    ImGui::SliderFloat("Cross Marker Size", &render.crossGizmo.size, 0.01f, 1.0f);
  }
}
