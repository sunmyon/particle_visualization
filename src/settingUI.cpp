#include "UI.h"
#include "settingUI.h"
#include "app/app_services.h"
#include "render_actions.h"

#include "interaction/camera.h"
#include "particle_visual_config.h"   // ParticleVisualConfig の実定義
#include "FileIO/file_io.h"       // FileInfo の実定義
#include "FileIO/file_format_dialog.h"       // FileInfo の実定義
#include "render/colormap_defs.h"  
#include "data/particle_array.h"

#include "FindClumps/find_clumps.h"             // FindClump

#ifdef PYTHON_BRIDGE
#include "PythonBridge/BridgeAdapter.h"
#include "PythonBridge/PythonBridge.h"
#include "PythonBridge/ShmLayout.h"
#endif

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
static void DrawParticleTypeSettingsSection(ParticleArray* P, ParticleVisualConfig& particleVisual);
static void DrawFileNavigationSection(SnapshotSource& source, FileInfo& fileInfo, ParticleArray* P, FileNavigationRuntimeState& rt, ToolWindowUIState& tools);
static void DrawNormalizationSection(ParticleArray* P);
static void DrawSinkIdSection(const CameraContext& camCtx, ParticleLabelRenderState& labels);
static void DrawCameraPlacementSection(ParticleArray* P, SettingsRuntimeState& rt);
#ifdef PYTHON_BRIDGE
static void DrawPythonBridgeSection(ParticleArray* Part, struct PythonBridgeState& py);
#endif
static void DrawAnalysisSection(SettingsUIContext& ctx, AnalysisRequestRuntimeState& rt, ToolWindowUIState& tools);
static void DrawRenderingSection(SettingsUIContext& ctx, AnalysisRequestRuntimeState& rt, ToolWindowUIState& tools);
static void DrawOtherSettingsSection(UnitSystem& units, FileInfo* fileInfo, SettingsRuntimeState& rt, RenderRuntimeState& render);

void ShowSettingsUI(SettingsUIContext& ctx, AppRuntimeState& rt) {
  ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysVerticalScrollbar);

  DrawCameraInfoSection(*ctx.camCtx);
  DrawParticleTypeSettingsSection(ctx.P, *ctx.particleVisual);
  DrawFileNavigationSection(ctx.fileInfo->editSource(), *ctx.fileInfo, ctx.P, rt.settings.fileNavigation, *ctx.windows);
  DrawNormalizationSection(ctx.P);
  DrawSinkIdSection(*ctx.camCtx, ctx.render->particleLabels);
  DrawCameraPlacementSection(ctx.P, rt.settings);
#ifdef PYTHON_BRIDGE
  DrawPythonBridgeSection(ctx.P, ctx.services->py);
#endif
  DrawAnalysisSection(ctx, rt.analysis, *ctx.windows);
  DrawRenderingSection(ctx, rt.analysis, *ctx.windows);
  DrawOtherSettingsSection(ctx.P->units, ctx.fileInfo, rt.settings, rt.render);

  ImGui::End();
}

static void DrawCameraInfoSection(const CameraContext& camCtx) {
  ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", camCtx.cameraPos.x, camCtx.cameraPos.y, camCtx.cameraPos.z);
  ImGui::Text("Camera Target:   (%.2f, %.2f, %.2f)", camCtx.cameraTarget.x, camCtx.cameraTarget.y, camCtx.cameraTarget.z);
}

static void DrawParticleTypeSettingsSection(ParticleArray* Part, ParticleVisualConfig& particleVisual) {
  if (!ImGui::CollapsingHeader("Particle Type Settings"))
    return;
    
  for (int i = 0; i < 6; i++) {
    std::string header = "Type " + std::to_string(i);
    if (ImGui::TreeNode(header.c_str())) {
      auto& cfg = particleVisual.types[i];
				
      std::string comboLabel = "Colormap##" + std::to_string(i);
      const char* preview = gColormapDefs[cfg.colormapIndex].name;
      if (ImGui::BeginCombo(comboLabel.c_str(), preview)) {
	for (int k = 0; k < gNumColormaps; ++k) {
	  bool selected = (cfg.colormapIndex == k);
	  if (ImGui::Selectable(gColormapDefs[k].name, selected)) {
	    cfg.colormapIndex = k;
	  }
	  if (selected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
      }
				
      ImGui::Checkbox("Periodic Color Bar", &cfg.periodicColorBar);
				
      std::string sliderLabel = "Point Size##" + std::to_string(i);
      ImGui::SliderFloat(sliderLabel.c_str(), &cfg.pointSize, 1.0f, 100.0f);
      std::string minLabel = "Value Min##" + std::to_string(i);
      ImGui::InputFloat(minLabel.c_str(), &cfg.colorMin, 0.01f, 0.1f, "%.3f");
      std::string maxLabel = "Value Max##" + std::to_string(i);
      ImGui::InputFloat(maxLabel.c_str(), &cfg.colorMax, 0.01f, 0.1f, "%.3f");
      std::string logLabel = "Use Log Scale##" + std::to_string(i);
      ImGui::Checkbox(logLabel.c_str(), &cfg.useLogScale);
				
      bool flagHideParticles_prev =  static_cast<bool>(cfg.hideParticles);
      std::string hideLabel = "Hide particle##" + std::to_string(i);
      ImGui::Checkbox(hideLabel.c_str(), &cfg.hideParticles);
      if(flagHideParticles_prev != cfg.hideParticles)
	Part->particlesDirty = true;
				
      QuantityId icolor_prev = cfg.selectedQuantity;
      QuantityId& sel = cfg.selectedQuantity;

      std::string quantityLabel = "Quantity##ptype_" + std::to_string(i);
      if (ImGui::BeginCombo(quantityLabel.c_str(), QuantityLabel(sel))) {
	for (int q = 0; q < Part->particleBlock.nUIQ; ++q) {
	  QuantityId cand = Part->particleBlock.uiQ[q];
	  bool is_selected = (cand == sel);
	  if (ImGui::Selectable(QuantityLabel(cand), is_selected)) sel = cand;
	  if (is_selected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
      }
				
      auto findIndex = [&](QuantityId q)->int{
	for (int k = 0; k < Part->particleBlock.nUIQ; ++k) if (Part->particleBlock.uiQ[k] == q) return k;
	return 0; // fallback
      };
				
      int qidx = findIndex(sel);
      ImGui::Text("Current particle %s range: [%g, %g]",
		  QuantityLabel(sel),
		  Part->particleValueMin[qidx][i],
		  Part->particleValueMax[qidx][i]);
				
      if(icolor_prev != cfg.selectedQuantity){
	Part->particlesDirty = true;  // グローバルなフラグをtrueに設定
      }
				
      ImGui::TreePop();
    }
  }    
}

static void DrawFileNavigationSection(SnapshotSource& source, FileInfo& fileInfo, ParticleArray* Part, FileNavigationRuntimeState& rt, ToolWindowUIState& tools){
  if(!ImGui::CollapsingHeader("File Navigation"))
    return;

  auto& src = source;
  
  ImGui::InputText("Folder", src.folderPath, IM_ARRAYSIZE(src.folderPath));
  ImGui::InputText("File Format", src.fileFormat, IM_ARRAYSIZE(src.fileFormat));
  ImGui::InputInt("initialIndex", &src.initialIndex);
		
  char fileNameOnly[255];
  std::snprintf(fileNameOnly, sizeof(fileNameOnly), src.fileFormat, src.initialIndex);
  std::snprintf(src.filePath, sizeof(src.filePath), "%s/%s", src.folderPath, fileNameOnly);
		
  if (ImGui::Button("Browse Files")) {
#ifndef NONATIVEFILEDIALOG
    nfdu8char_t* outPath = nullptr;

    nfdu8filteritem_t filters[] = {
      { "Snapshot files", "hdf5,h5,bin,dat,*" }
    };

    nfdopendialogu8args_t args = {};
    //args.filterList  = filters;
    //args.filterCount = 1;
    args.filterList  = nullptr; 
    args.filterCount = 0;    
    args.defaultPath = src.folderPath[0] ? src.folderPath : nullptr;

    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY) {
      fileInfo.applySelectedFilePath(outPath);
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
    config.path = src.folderPath;
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
	  fileInfo.applySelectedFilePath(selectedFile.c_str());
	}
      else
	{
	  ImGui::Text("File Dialog Cancelled");
	}
      ImGuiFileDialog::Instance()->Close();
    }
#endif
  
  src.currentFileIndex = src.initialIndex + src.currentStep * src.skipStep;
  ImGui::Text("File: %s", src.filePath);
  ImGui::Text("Current File: %d", src.currentFileIndex);
		
  ImGui::BeginDisabled(fileInfo.isLoading());

  if (rt.tempSkipStep <= 0) {
    rt.tempSkipStep = src.skipStep;
  }

  if (ImGui::InputInt("Skip Step", &rt.tempSkipStep, 1, 100)) {
    rt.request.applySkipStepRequested = true;
  }

  if (ImGui::InputInt("Select File Index", &src.currentStep, 1, 10)) {
    rt.request.loadSelectedSnapshotRequested = true;
  }

  if (ImGui::Button("Previous File") && src.currentStep > 0) {
    rt.request.loadPreviousRequested = true;
  }

  ImGui::SameLine();

  if (ImGui::Button("Next File")) {
    rt.request.loadNextRequested = true;
  }

  if (ImGui::InputInt("Batch Size", &src.batchSize)) {
    rt.request.loadBatchRequested = true;
  }

  ImGui::EndDisabled();

  if (fileInfo.isLoading()) {
    ImGui::Text("Loading...");
  }

  if (ImGui::Button("Reload")) {
    rt.request.reloadRequested = true;
  }

  if (ImGui::Button("Edit Data Format")) {
#ifdef HAVE_HDF5
    if(source.useHDF5)
      OpenHDF5FormatDialog(tools.fileFormatDialog, fileInfo.getSource());
    else
#endif
      OpenBinaryFormatDialog(tools.fileFormatDialog, fileInfo.getSource());    
  }

  static const char* FileFormatNames[] = {
    "Auto", "HDF5", "Binary", "Gadget", "Framed"
  };
  static_assert(static_cast<int>(FileFormat::_Count) == IM_ARRAYSIZE(FileFormatNames),
                "FileFormatNames needs to match FileFormat::_Count");

  int fmtIdx = fileInfo.getFormatMode_int();
  if (ImGui::Combo("Read data format", &fmtIdx, FileFormatNames, IM_ARRAYSIZE(FileFormatNames))) {
    fileInfo.setFormatMode(static_cast<FileFormat>(fmtIdx));
  }

  if (ImGui::Button("Mask Settings...")) {
    OpenMaskUI(tools.mask);
  }

  if (ImGui::Button("Generate test data")) {
    rt.request.generateTestDataRequested = true;
  }
}
  

static void DrawNormalizationSection(ParticleArray* Part) {
  if (!ImGui::CollapsingHeader("Normalization"))
    return;

  ImGui::InputFloat("Desired Maximum", &Part->desiredMax, 0.f, 0.f, "%g");
  if (ImGui::Button("Normalize Positions"))
    Part->rescalePositions();

  ImGui::Text("Original max coordinate: %.3g", Part->originalMax);
  ImGui::Text("Max coordinate is normalized to: %.3f", Part->desiredMax);
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

static void DrawCameraPlacementSection(ParticleArray* Part,
                                       SettingsRuntimeState& rt)
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

  ImGui::InputFloat("Culling radius", &rt.radiusCullingSphere);

  if (ImGui::Button("Culling sphere region")) {
    req.applyCullingRequested = true;
  }

  if (ImGui::Button("disable Culling")) {
    req.clearCullingRequested = true;
  }
}

#ifdef PYTHON_BRIDGE
static void DrawPythonBridgeSection(ParticleArray* Part, struct PythonBridgeState& py){
  if (!ImGui::CollapsingHeader("Python:Jupyter notebook")) 
    return;
  
  const bool isOpen = (py.ptr != nullptr);
  if (ImGui::Button(isOpen ? "Close notebook" : "Open notebook")) {
    if (!isOpen) {
      // --- Open ---
      py.ptr.reset(CreatePythonBridge());
      if (!py.ptr) {
	ImGui::TextColored(ImVec4(1,0.4f,0.4f,1),"Bridge creation failed");
      } else {
	// 共有メモリ準備
	const uint64_t N = static_cast<uint64_t>(Part->particleBlock.particles.size());
	if (!py.ptr->init(N, /*withB=*/true, "cppvis_pos")) {
	  ImGui::TextColored(ImVec4(1,0.4f,0.4f,1),"Bridge init failed");
	  py.ptr.reset();
	} else {
	  bridge::loadInitialFromAoS(*py.ptr, *Part, sizeof(ParticleData));	    
	  // Notebook 起動（非同期でもOK／boolで可否だけ握る）
	  py.launched = py.ptr->launchNotebook("./jupyter_work");
	}
      }
    } else {
      // --- Close ---
      py.ptr->shutdown();
      py.ptr.reset();
      py.launched = false;
      py.needUploadPos = false;
    }
  }
		
  // ステータス表示
  if (py.ptr) {
    ImGui::SameLine();
    ImGui::TextColored(py.launched ? ImVec4(0.6f,1,0.6f,1) : ImVec4(1,0.8f,0.4f,1),
		       py.launched ? "launched" : "launching...");
  }
		
  if (py.ptr && py.launched) {
    const auto& info = py.ptr->notebookInfo();
			
    ImGui::SeparatorText("Jupyter Notebook");
    ImGui::Text("Port : %d", info.port);
    ImGui::TextWrapped("URL  : %s", info.url.c_str());
			
    // クリップボードにコピー
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy URL")) {
      ImGui::SetClipboardText(info.url.c_str());
    }
			
    // 既に JupyterLauncher で open/xdg-open 済みでも、手動で開けるボタンを用意
    if (ImGui::SmallButton("Open in Browser")) {
#if defined(__APPLE__)
      std::string cmd = "open \"" + info.url + "\"";
      std::system(cmd.c_str());
#elif defined(__linux__)
      std::string cmd = "xdg-open \"" + info.url + "\"";
      std::system(cmd.c_str());
#elif defined(_WIN32)
      // Windows: start はシェル内蔵。cmd /c 経由で。
      std::string cmd = "cmd /c start \"\" \"" + info.url + "\"";
      std::system(cmd.c_str());
#endif
    }
			
    // 状態表示（任意）
    ImGui::SameLine();
    ImGui::TextDisabled("(token: %s)", info.token.c_str());
  }
}
#endif

static void DrawAnalysisSection(SettingsUIContext& ctx, AnalysisRequestRuntimeState& rt, ToolWindowUIState& tools){
  if (!ImGui::CollapsingHeader("Analysis"))
    return;

  ParticleArray* Part = ctx.P;
  CameraContext& camCtx = *ctx.camCtx;
  auto& source = ctx.fileInfo->editSource();
  auto* services = ctx.services;
  auto* radialProfile = services->radialProfile.get();
  auto* histogram2D   = services->histogram2D.get();
  auto* clumpFind     = services->clumpFind.get();
  auto* render = ctx.render;
  auto* analysis = ctx.analysis;
  
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
      clumpFind->showWindow();

#ifdef CLUMP_DATA_READ
    auto& batchReq = rt.clumpBatch;
    auto& batchRes = ctx.analysis->clumpBatch;

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
		   source.folderPath,
		   IM_ARRAYSIZE(batchReq.outputFolderPath));
      batchReq.outputFolderPath[IM_ARRAYSIZE(batchReq.outputFolderPath) - 1] = '\0';
    }

    if (ImGui::Button("generate clump data")) {
      batchReq.runRequested = true;
    }

    if (batchRes.completed) {
      ImGui::Text("Processed snapshots: %d", batchRes.processedSnapshots);
      ImGui::Text("Output: %s", batchRes.outputPath);
    }

    if (batchRes.errorMessage[0] != '\0') {
      ImGui::TextColored(ImVec4(1,0,0,1), "%s", batchRes.errorMessage);
    }

    if(ImGui::Button("show clump list"))
      clumpFind->showClumpListWindow();

    if(ImGui::Button("show clump chain list")){
      std::string fname(batchReq.outputFolderPath);
      fname += "/";
      fname += batchReq.outputFileName;
      clumpFind->showWindowClumpChainList(source.initialIndex,
					  batchReq.nSnapshots,
					  source.skipStep,
					  fname);
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
    auto& singleRes = ctx.analysis->disk;
    
    ImGui::SeparatorText("Single disk analysis");

    ImGui::InputInt("Particle ID1##disk", &singleReq.targetParticleId);
    ImGui::SliderFloat("Opacity##disk", &render->disks.opacity, 0.0f, 1.0f);

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
    auto& batchRes  = ctx.analysis->diskBatch;    
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

    if (batchRes.completed) {
      ImGui::Text("Processed rows: %d", batchRes.processedRows);
    }

    break;
  }

  case ANALYSIS_ISO_DENSITY: {
    auto& singleReq = rt.ellipsoid;
    auto& singleRes = ctx.analysis->ellipsoid;
    auto& batchReq  = rt.ellipsoidBatch;
    auto& batchRes  = ctx.analysis->ellipsoidBatch;

    ImGui::SeparatorText("Single ellipsoid analysis");

    ImGui::InputInt("Particle ID1", &singleReq.particleId1);
    ImGui::InputInt("Particle ID2", &singleReq.particleId2);
    ImGui::SliderFloat("Opacity##contour_ellipse", &render->ellipsoids.opacity, 0.0f, 1.0f);

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

    if (batchRes.completed) {
      ImGui::Text("Processed rows: %d", batchRes.processedRows);
    }

    break;
  }			
#endif      
  }
}


static void DrawRenderingSection(SettingsUIContext& ctx, AnalysisRequestRuntimeState& rt, ToolWindowUIState& tools){
  if (!ImGui::CollapsingHeader("Rendering"))
    return;

  ParticleArray* Part = ctx.P;
  CameraContext& camCtx = *ctx.camCtx;

  auto* services = ctx.services;
  auto* render = ctx.render;
  
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
    
    auto& movieReq = rt.projectionMovie;
    auto& movieRes = ctx.analysis->projectionMovie;

    ImGui::Text("create projection maps for continuous snapshots");

    ImGui::InputInt("number of snapshots##render", &movieReq.nSnapshots);
    ImGui::InputText("Output File Format##render",
		     movieReq.outputFileFormat,
		     IM_ARRAYSIZE(movieReq.outputFileFormat));
    ImGui::InputText("Output Folder##render",
		     movieReq.outputFolderPath,
		     IM_ARRAYSIZE(movieReq.outputFolderPath));
    ImGui::InputText("Output Name of Movie##render",
		     movieReq.outputMovieName,
		     IM_ARRAYSIZE(movieReq.outputMovieName));

    ImGui::Checkbox("show face-on view", &movieReq.faceOn);

    ImGui::Checkbox("follow the center around the particle", &movieReq.followSinkCenter);
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

    if (ImGui::Button("generate maps")) {
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
    ImGui::SliderFloat("Opacity", &render->isocontour.opacity, 0.0f, 1.0f);
    ImGui::SliderInt("Maximum level of OctTree", &req.maxTreeLevel, 5, 20);

    if (ImGui::BeginCombo("Quantity for Iso-Contour",
			  QuantityLabel(req.selectedQuantity))) {
      for (int q = 0; q < Part->particleBlock.nUIQ; ++q) {
	QuantityId cand = Part->particleBlock.uiQ[q];
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
    ImGui::InputInt("show velocity field out of n particles", &render->velocity.subtraction);
    ImGui::InputFloat("Arrow Scale", &render->velocity.arrowScale, 0.1f, 1.0f, "%.2f");
    ImGui::Checkbox("Use Log Scale", &render->velocity.useLogScale);
				
    if(ImGui::Checkbox("render velocity field", &render->velocity.show)){
      if(render->velocity.show)
	Part->velocityDirty = true;
    }
    break;
  }  
  }
}


static void DrawOtherSettingsSection(UnitSystem& units, FileInfo* fileInfo, SettingsRuntimeState& rt, RenderRuntimeState& render)
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
    fileInfo->setUnit(units);
  }

  if (ImGui::CollapsingHeader("Zoom Range")) {
    ImGui::InputFloat("Min Zoom", &rt.minZoom, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Max Zoom", &rt.maxZoom, 0.0f, 0.0f, "%g");
  }

  if (ImGui::CollapsingHeader("Cross Marker")) {
    ImGui::SliderFloat("Cross Marker Size", &render.crossGizmo.size, 0.01f, 1.0f);
  }
}

