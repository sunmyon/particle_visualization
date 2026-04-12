#include "UI.h"
#include "app/app_services.h"
#include "render_actions.h"

#include "interaction/camera.h"
#include "particle_visual_config.h"   // ParticleVisualConfig の実定義
#include "FileIO/file_io.h"       // FileInfo の実定義
#include "render/colormap_defs.h"  
#include "data/particle_array.h"

#include "FindClumps/find_clumps.h"             // FindClump

#ifdef STREAM_LINE
#include "StreamLine/stream_line_new.h"
#endif

#ifdef PYTHON_BRIDGE
#include "PythonBridge/BridgeAdapter.h"
#include "PythonBridge/PythonBridge.h"
#include "PythonBridge/ShmLayout.h"
#endif

#ifdef GEOMETRICAL_ANALYSIS
#include "GeometricAnalysis/ellipse_fitter.h"
#include "GeometricAnalysis/DiskRadius.hpp"
#endif

#ifdef VOLUME_RENDERING
#include "BVH/BVH.hpp"
#include "VolumeRendering/tau_sph.h"
#include "VolumeRendering/TransferFunctionEditor.hpp"
#include "VolumeRendering/OpacityComputer.hpp"
#endif

#ifdef ISO_CONTOUR
#include "IsoSurface/iso_contour_build.h"
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

namespace {
  std::pair<std::string, int> convertFilenameToFormatAndExtractNumber(const std::string& filename)
  {
    size_t dotPos = filename.find_last_of('.');
    std::string basename = (dotPos == std::string::npos) ? filename : filename.substr(0, dotPos);
    std::string extension = (dotPos == std::string::npos) ? "" : filename.substr(dotPos); 

    // 最後の数字がある位置を探す
    size_t pos = basename.find_last_of("0123456789");
    if (pos == std::string::npos)
      return std::make_pair(filename, 0); // 数字が見つからなければそのまま返す

    // 数字部分の開始位置を後ろから辿って求める
    size_t numEnd = pos;
    size_t numStart = pos;
    while (numStart > 0 && std::isdigit(basename[numStart - 1])) {
      numStart--;
    }
    size_t numLen = numEnd - numStart + 1;

    // ファイル名の前半部分（数字部分以前）を取得
    std::string prefix = basename.substr(0, numStart);
    // この例では数字部分以降の文字列は取り除く（必要に応じて拡張子などを扱えます）
    std::string suffix = "";

    // 桁数に合わせたフォーマット指定子を作成（例: 3桁なら "%03d"）
    std::string formatSpecifier = "%" + std::string("0") + std::to_string(numLen) + "d";

    std::string newFormat = prefix + formatSpecifier + suffix + extension;

    // 数字部分を整数に変換
    int fileNumber = -1;
    try {
      fileNumber = std::stoi(basename.substr(numStart, numLen));
    } catch (const std::exception& e) {
      fileNumber = -1;
    }
    
    return std::make_pair(newFormat, fileNumber);
  }
}

static void DrawCameraInfoSection(const CameraContext& camCtx);
static void DrawParticleTypeSettingsSection(ParticleArray* P, ParticleVisualConfig& particleVisual);
static void DrawFileNavigationSection(FileInfo* fileInfo, ParticleArray* P);
static void DrawNormalizationSection(ParticleArray* P);
static void DrawSinkIdSection(const CameraContext& camCtx, ParticleLabelRenderState& labels);
static void DrawCameraPlacementSection(ParticleArray* P, CameraContext& camCtx, SettingsRuntimeState& rt);
#ifdef PYTHON_BRIDGE
static void DrawPythonBridgeSection(ParticleArray* Part, struct PythonBridgeState& py);
#endif
static void DrawAnalysisSection(SettingsUIContext& ctx, SettingsRuntimeState& rt);
static void DrawRenderingSection(SettingsUIContext& ctx, SettingsRuntimeState& rt);
static void DrawOtherSettingsSection(ParticleArray* Part, FileInfo* fileInfo, SettingsRuntimeState& rt, RenderRuntimeState& render);

void ShowSettingsUI(SettingsUIContext& ctx, SettingsRuntimeState& rt) {
  ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysVerticalScrollbar);

  DrawCameraInfoSection(*ctx.camCtx);
  DrawParticleTypeSettingsSection(ctx.P, *ctx.particleVisual);
  DrawFileNavigationSection(ctx.fileInfo, ctx.P);
  DrawNormalizationSection(ctx.P);
  DrawSinkIdSection(*ctx.camCtx, ctx.render->particleLabels);
  DrawCameraPlacementSection(ctx.P, *ctx.camCtx, rt);
#ifdef PYTHON_BRIDGE
  DrawPythonBridgeSection(ctx.P, ctx.services->py);
#endif
  DrawAnalysisSection(ctx, rt);
  DrawRenderingSection(ctx, rt);
  DrawOtherSettingsSection(ctx.P, ctx.fileInfo, rt, *ctx.render);

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

static void DrawFileNavigationSection(FileInfo* fileInfo, ParticleArray* Part){
  if(!ImGui::CollapsingHeader("File Navigation"))
    return;
  
  ImGui::InputText("Folder", fileInfo->folderPath, IM_ARRAYSIZE(fileInfo->folderPath));
  ImGui::InputText("File Format", fileInfo->fileFormat, IM_ARRAYSIZE(fileInfo->fileFormat));
  ImGui::InputInt("initialIndex", &fileInfo->initialIndex);
		
  char fileNameOnly[255];
  std::snprintf(fileNameOnly, sizeof(fileNameOnly), fileInfo->fileFormat, fileInfo->initialIndex);
  std::snprintf(fileInfo->filePath, sizeof(fileInfo->filePath), "%s/%s", fileInfo->folderPath, fileNameOnly);
		
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
    args.defaultPath = fileInfo->folderPath[0] ? fileInfo->folderPath : nullptr;

    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

    if (result == NFD_OKAY) {
      std::strncpy(fileInfo->filePath, outPath, IM_ARRAYSIZE(fileInfo->filePath));
      fileInfo->filePath[IM_ARRAYSIZE(fileInfo->filePath) - 1] = '\0';
      NFD_FreePathU8(outPath);

      // フォルダ部分を抽出
      char* lastSlash = std::strrchr(fileInfo->filePath, '/');
#ifdef _WIN32
      char* lastBackslash = std::strrchr(fileInfo->filePath, '\\');
      if (!lastSlash || (lastBackslash && lastBackslash > lastSlash)) {
        lastSlash = lastBackslash;
      }
#endif

      if (lastSlash) {
        size_t folderLen = static_cast<size_t>(lastSlash - fileInfo->filePath + 1);
        std::strncpy(fileInfo->folderPath, fileInfo->filePath, folderLen);
        fileInfo->folderPath[folderLen] = '\0';
      }

      char* filename = fileInfo->filePath;
      if (lastSlash) filename = lastSlash + 1;
      
#ifdef HAVE_HDF5
      fileInfo->useHDF5 = false;
      char* dot = std::strrchr(filename, '.');
      if (dot) {
        std::string ext(dot);
        if (ext == ".hdf5" || ext == ".h5")
          fileInfo->useHDF5 = true;
      }
#endif
      auto res = convertFilenameToFormatAndExtractNumber(filename);
      std::strncpy(fileInfo->fileFormat, res.first.c_str(), IM_ARRAYSIZE(fileInfo->fileFormat));
      fileInfo->fileFormat[IM_ARRAYSIZE(fileInfo->fileFormat) - 1] = '\0';
      fileInfo->initialIndex = res.second;
    }
    else if (result == NFD_CANCEL) {
      // do nothing
    }
    else {
      std::cerr << "Error: " << NFD_GetError() << std::endl;
    }
#else
    IGFD::FileDialogConfig config;
    // 初期ディレクトリの設定（"path" メンバー）
    //config.path = fileInfo->filePath;
    config.path = fileInfo->folderPath;
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
	  std::string currentFolder = ImGuiFileDialog::Instance()->GetCurrentPath();
	  std::strncpy(fileInfo->filePath, selectedFile.c_str(), IM_ARRAYSIZE(fileInfo->filePath));
	  fileInfo->filePath[IM_ARRAYSIZE(fileInfo->filePath)-1] = '\0';
				
	  // 1. フォルダ部分を抽出
	  char* lastSlash = std::strrchr(fileInfo->filePath, '/');
	  if (lastSlash) {
	    size_t folderLen = lastSlash - fileInfo->filePath + 1; // '/'を含む
	    std::strncpy(fileInfo->folderPath, fileInfo->filePath, folderLen);
	    fileInfo->folderPath[folderLen] = '\0';	    
	  }
				
	  char *filename = fileInfo->filePath;
	  if(lastSlash)
	    filename = lastSlash + 1;
				
#ifdef HAVE_HDF5
	  // 2. ファイル形式（拡張子）の抽出
	  fileInfo->useHDF5 = false;
	  char* dot = std::strrchr(filename, '.');
	  if (dot) {
	    std::string ext(dot);
	    if (ext == ".hdf5") 
	      fileInfo->useHDF5 = true;	  
	  }
#endif
				
	  auto res = convertFilenameToFormatAndExtractNumber(filename);       
	  std::strncpy(fileInfo->fileFormat, res.first.c_str(), IM_ARRAYSIZE(fileInfo->fileFormat));
	  fileInfo->fileFormat[IM_ARRAYSIZE(fileInfo->fileFormat) - 1] = '\0'; 
	  fileInfo->initialIndex = res.second;	
	}
      else
	{
	  ImGui::Text("File Dialog Cancelled");
	}
      ImGuiFileDialog::Instance()->Close();
    }
#endif
		
  fileInfo->currentFileIndex = fileInfo->initialIndex + fileInfo->currentStep * fileInfo->skipStep;
  ImGui::Text("File: %s", fileInfo->filePath);
  ImGui::Text("Current File: %d", fileInfo->currentFileIndex);
		
  ImGui::BeginDisabled(fileInfo->isLoading);
		
  // **skipStep の調整**
  static int tempSkipStep = fileInfo->skipStep;
  if (ImGui::InputInt("Skip Step", &tempSkipStep, 1, 100)) {
    int newStep = std::round((fileInfo->currentFileIndex - fileInfo->initialIndex) / static_cast<float>(tempSkipStep));
    fileInfo->currentStep = std::max(0, newStep);
    fileInfo->skipStep = tempSkipStep;
			
    int newFileIndex = fileInfo->initialIndex + fileInfo->currentStep * fileInfo->skipStep;
    if (!fileInfo->isLoading)
      fileInfo->loadBatch(newFileIndex, fileInfo->batchSize, fileInfo->skipStep, Part);      
			
    fileInfo->currentFileIndex = newFileIndex;
  }
		
  // **スライダーで `fileInfo->currentFileIndex` を選択**
  if (ImGui::InputInt("Select File Index", &fileInfo->currentStep, 1, 10)) {
    int newFileIndex = fileInfo->initialIndex + fileInfo->currentStep * fileInfo->skipStep;
    fileInfo->loadNewSnapshot(newFileIndex, Part);      
  }
		
  // **前のファイル**
  if (ImGui::Button("Previous File") && fileInfo->currentStep > 0) {
    fileInfo->currentStep--;
			
    int newFileIndex = fileInfo->initialIndex + fileInfo->currentStep * fileInfo->skipStep;
    fileInfo->loadNewSnapshot(newFileIndex, Part);            
  }
		
  ImGui::SameLine();
		
  // **次のファイル**
  if (ImGui::Button("Next File")) {
    fileInfo->currentStep++;
			
    int newFileIndex = fileInfo->initialIndex + fileInfo->currentStep * fileInfo->skipStep;
    fileInfo->loadNewSnapshot(newFileIndex, Part);            
  }
		
  if(ImGui::InputInt("Batch Size", &fileInfo->batchSize)){
    if (!fileInfo->isLoading)
      fileInfo->loadBatch(fileInfo->currentStep, fileInfo->batchSize, fileInfo->skipStep, Part);      
  }
		
  ImGui::EndDisabled();
		
  // **ロード状況を表示**
  if (fileInfo->isLoading) {
    ImGui::Text("Loading...");
  }
		
  if (ImGui::Button("Reload")) {
    if (!fileInfo->isLoading) {
      int newFileIndex = fileInfo->initialIndex + fileInfo->currentStep * fileInfo->skipStep;
				
      fileInfo->loadBatch(fileInfo->currentStep, fileInfo->batchSize, fileInfo->skipStep, Part);      
      std::cout << "Reloaded files starting at file " << newFileIndex << std::endl;
    }
  }
		
  if (ImGui::Button("Edit Data Format")) {
#ifdef HAVE_HDF5
    if (fileInfo->useHDF5 || fileInfo->getFormatMode() == static_cast<int>(FileFormat::HDF5))
      fileInfo->showHDF5Dialog();
    else
#endif
      fileInfo->showDialog();      
  }
		
  static const char* FileFormatNames[] = {
    "Auto", "HDF5", "Binary", "Gadget", "Framed"
  };
  // FileFormat::_Count と同じ長さにしておく
  static_assert(static_cast<int>(FileFormat::_Count) == IM_ARRAYSIZE(FileFormatNames), 
		"FileFormatNames needs to match FileFormat::_Count");
		
  int fmtIdx = fileInfo->getFormatMode();
  // シンプルに Combo で切り替え
  if (ImGui::Combo("Read data format", &fmtIdx, FileFormatNames, IM_ARRAYSIZE(FileFormatNames))) {
    // ユーザーが選び直したら enum に戻す
    fileInfo->setFormatMode(static_cast<FileFormat>(fmtIdx));
  }
		
  if (ImGui::Button("Mask Settings...")) {
    OpenMaskUI();
  }
  
  if (ImGui::Button("Generate test data")) {
    fileInfo->generateTestData(Part);      
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

static void DrawCameraPlacementSection(ParticleArray* Part, CameraContext& camCtx, SettingsRuntimeState& rt){
  if (!ImGui::CollapsingHeader("Set camera pos"))
    return;
  
  static float centerInput[3] = {0.0f, 0.0f, 0.0f};
  static bool inputIsOriginal = true;
  ImGui::InputFloat3("Center Coordinates", centerInput, "%.3f");
  ImGui::Checkbox("Input in Original Coordinates", &inputIsOriginal);
  if (ImGui::Button("Set Center")) {
    float distance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
    glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
			
    if (inputIsOriginal) {
      // 入力された座標は original 座標なので、正規化に使用している倍率をかけて normalized 座標に変換
      camCtx.cameraTarget = glm::vec3(centerInput[0], centerInput[1], centerInput[2]) * Part->normalizationFactor;
    } else {
      camCtx.cameraTarget = glm::vec3(centerInput[0], centerInput[1], centerInput[2]);
    }
    
    camCtx.cameraPos = camCtx.cameraTarget - direction * distance;
  }
		
  static int currentView = 0;
  const char* viewDirections[] = {
    "View from +X", "View from -X",
    "View from +Y", "View from -Y",
    "View from +Z", "View from -Z"
  };
  ImGui::Combo("Projection Direction", &currentView, viewDirections, IM_ARRAYSIZE(viewDirections));
		
  static float rollAngle = 0.0f; // ロール角度（度単位）
  ImGui::SliderFloat("Roll Angle (deg)", &rollAngle, -180.0f, 180.0f, "%.1f");
		
  if (ImGui::Button("Set Projection")) {
    float distance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
			
    switch (currentView) {
    case 0: // +X
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(distance, 0.0f, 0.0f);
      camCtx.cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
      break;
    case 1: // -X
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(-distance, 0.0f, 0.0f);
      camCtx.cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
      break;
    case 2: // +Y
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, distance, 0.0f);
      camCtx.cameraUp = glm::vec3(0.0f, 0.0f, -1.0f); // Z軸を上方向に
      break;
    case 3: // -Y
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, -distance, 0.0f);
      camCtx.cameraUp = glm::vec3(0.0f, 0.0f, 1.0f); // 反対Z軸を上方向に
      break;
    case 4: // +Z
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, 0.0f, distance);
      camCtx.cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
      break;
    case 5: // -Z
      camCtx.cameraPos = camCtx.cameraTarget + glm::vec3(0.0f, 0.0f, -distance);
      camCtx.cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
      break;
    }
			
    // 視線方向ベクトルを取得
    glm::vec3 viewDir = glm::normalize(camCtx.cameraTarget - camCtx.cameraPos);
    // 回転クォータニオン生成（右手系、正の角度で反時計回り）
    glm::quat rollQuat = glm::angleAxis(glm::radians(rollAngle), viewDir);
    // Upベクトルに適用
    camCtx.cameraUp = rollQuat * camCtx.cameraUp;
			
    // ビュー行列を更新
    glm::mat4 view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);
			
    // クォータニオンも更新（必要な場合）
    camCtx.cameraOrientation = glm::quat_cast(glm::inverse(view));
  }
		
  ImGui::InputFloat("Culling radius", &rt.radiusCullingSphere);    
  if(ImGui::Button("Culling sphere region")){
    for(size_t i=0;i<Part->particleBlock.particles.size();i++){
      auto &p = Part->particleBlock.particles[i];
      uint8_t flag_mask = 0;
      if(glm::distance(glm::vec3(p.pos[0], p.pos[1], p.pos[2]), camCtx.cameraTarget) > rt.radiusCullingSphere)
	flag_mask = 1;
				
      Part->flag_mask[i] = flag_mask;
    }
			
    Part->particlesDirty = true; 
  }
		
  if(ImGui::Button("disable Culling")){
    for(size_t i=0;i<Part->particleBlock.particles.size();i++)
      Part->flag_mask[i] = 0;            
    Part->particlesDirty = true; 
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

static void DrawAnalysisSection(SettingsUIContext& ctx, SettingsRuntimeState& rt){
  if (!ImGui::CollapsingHeader("Analysis"))
    return;

  ParticleArray* Part = ctx.P;
  CameraContext& camCtx = *ctx.camCtx;
  FileInfo* fileInfo = ctx.fileInfo;
  auto& particleVisual = *ctx.particleVisual;

  auto* services = ctx.services;
  auto* radialProfile = services->radialProfile.get();
  auto* histogram2D   = services->histogram2D.get();
  auto* clumpFind     = services->clumpFind.get();
#ifdef GEOMETRICAL_ANALYSIS
  auto* diskFinder    = services->diskFinder.get();
  auto* ellipsoid     = services->ellipsoid.get();
#endif
  auto* render = ctx.render;
  auto* scene = ctx.scene;
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
      OpenRadialProfileUI();
    DrawRadialProfileUI(*radialProfile, Part->particleBlock, Part->UnitMass_in_g, Part->UnitLength_in_cm, Part->UnitTime_in_s);       
    break;
  }
  case ANALYSIS_2D_HISTOGRAM: {
    if (ImGui::Button("Compute 2D histogram"))
      OpenHistogram2DUI();

    Histogram2DContext histCtx;
    histCtx.cameraCenter = &camCtx.cameraTarget;
    
    auto visibleHulls = analysis->convexHulls.visibleHulls();
    histCtx.convexHulls = &visibleHulls;

    DrawHistogram2DUI(*histogram2D, Part->particleBlock, histCtx);
    break;
  }
  case ANALYSIS_CLUMP_FIND: {
    if (ImGui::Button("Run Clumps finder")) 
      clumpFind->showWindow();
				
#ifdef CLUMP_DATA_READ
    ImGui::Text("create clump data for continuous snapshots");
				
    static int method = 0;  
				
    // ラジオボタン
    ImGui::RadioButton("FOF",       &method, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Dendrogram",&method, 1);
				
    static int nsnapshots = 10;
    static char outputFileName[255]="clump_data.hdf5";
    static char outputFolderPath[255]="./output/";
    ImGui::InputInt("number of snapshots##FOF", &nsnapshots);
    ImGui::InputText("Output File Name##FOF", outputFileName, IM_ARRAYSIZE(outputFileName));
    ImGui::InputText("Output Folder##FOF", outputFolderPath, IM_ARRAYSIZE(outputFolderPath));
				
    char filename[512];
    snprintf(filename, sizeof(filename), "%s/%s", outputFolderPath, outputFileName);
				
    ImGui::SameLine();
    if (ImGui::Button("default path")) {
      strcpy(outputFolderPath, fileInfo->folderPath);
    }
				
    if(ImGui::Button("generate clump data")){
      int savedStep = fileInfo->currentStep;
					
      clumpFind->initialize_prev_nodes();      
      for(int i=0;i<nsnapshots;i++){
	fileInfo->currentStep = savedStep;
	if(i > 0) fileInfo->currentStep += i;
	
	int newFileIndex = fileInfo->initialIndex + fileInfo->currentStep * fileInfo->skipStep;
	fileInfo->loadNewSnapshot(newFileIndex, Part);            
	
	if(Part->particleBlock.particles.size() == 0)
	  continue;
	
	clumpFind->do_FOF_and_output_clump_data(method, Part->particleBlock.particles, Part->particleBlock.header, filename, newFileIndex);
      }
					
      fileInfo->currentStep = savedStep;
      fileInfo->currentFileIndex = fileInfo->initialIndex + fileInfo->currentStep * fileInfo->skipStep;
      
      int initstep = fileInfo->currentFileIndex;
      int dstep = fileInfo->skipStep;
      std::string fname(filename);
      clumpFind->give_stellar_id_to_clumps(initstep, nsnapshots, dstep, fname);
    }
				
    if(ImGui::Button("show clump list"))
      clumpFind->showClumpListWindow();
				
    if(ImGui::Button("show clump chain list")){
      std::string fname(filename);
      clumpFind->showWindowClumpChainList(fileInfo->initialIndex, nsnapshots, fileInfo->skipStep, fname);
    }
#endif
    break;
  }
			
  case ANALYSIS_STELLAR_DENSITY: {
    static bool selType[6] = { false, false, false, true, true, true };
				
    ImGui::Text("Particle types to include:");
    ImGui::Checkbox("Type 0##stellar_density", &selType[0]); ImGui::SameLine();
    ImGui::Checkbox("Type 1##stellar_density", &selType[1]); ImGui::SameLine();
    ImGui::Checkbox("Type 2##stellar_density", &selType[2]);
    ImGui::Checkbox("Type 3##stellar_density", &selType[3]); ImGui::SameLine();
    ImGui::Checkbox("Type 4##stellar_density", &selType[4]); ImGui::SameLine();
    ImGui::Checkbox("Type 5##stellar_density", &selType[5]);
				
    static bool flag_overwrite_hsml = false;
    ImGui::Checkbox("overwrite hsml##stellar_density", &flag_overwrite_hsml);
				
    if (ImGui::Button("Select 3,4,5##stellar_density")) {
      for (int t = 0; t < 6; ++t) selType[t] = false;
      selType[3] = selType[4] = selType[5] = true;
    }
				
    if (ImGui::Button("Compute stellar density##stellar_density")) {
      std::array<bool,6> sel{};
      for (int t=0;t<6;++t) sel[t] = selType[t];
					
      Part->computeStellarDensity(sel, flag_overwrite_hsml);
      Part->particlesDirty = true;  // グローバルなフラグをtrueに設定
    }
    break;
  }
			
			
#ifdef HAVE_HDF5
  case ANALYSIS_HALO_CATALOGUE: {
    if(ImGui::Button("Load Halo"))
      OpenHaloesUI();
    DrawHaloesUI(Part, camCtx, fileInfo);
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
    static int queryID_disk=0;
    ImGui::InputInt("Particle ID1##disk", &queryID_disk);
    ImGui::SliderFloat("Opacity##disk", &render->disks.opacity, 0.0f, 1.0f); 
				
    DiskRadiusFinder::Params param_disk;
				
    if (ImGui::Button("Find a disk around the paritlce")) {
      bool flag_found = false;
      for(auto &p : Part->particleBlock.particles){
	if(p.ID == queryID_disk){
	  param_disk.mass = p.mass;
	  for(int k=0;k<3;k++){
	    param_disk.center[k] = p.pos[k];
	    param_disk.v_center[k] = p.vel[k];
	  }
	  flag_found = true;
	}
						
	if(flag_found)
	  break;
      }

      if (flag_found) {
	param_disk.G = Part->GravConst_internal;
	param_disk.max_shell = 100;
	param_disk.scale_fac = Part->originalMax / Part->desiredMax;
	
	DiskObject disk;
	disk.color = glm::vec3(1.0f, 1.0f, 1.0f);
	disk.opacity = render->disks.opacity;
	disk.tag = "main_disk";
	
	scene->disk.clearGroup("main_disk");
	
	if (diskFinder->compute(Part->particleBlock.particles, param_disk, disk)) {
	  scene->disk.add(disk);
	}
      }      
    }
				
    if (ImGui::Button("disable disks")) {
      scene->disk.clear();
    }
				
    static char fname_input[255]="binary_fragmentation_ellipticity_all_w_mode.txt";
    static char fname_output[255]="binary_fragmentation_disks.txt";
    ImGui::InputText("Read target from text file##disk", fname_input, IM_ARRAYSIZE(fname_input));
    ImGui::InputText("Output target from text file##disk", fname_output, IM_ARRAYSIZE(fname_output));
				
    if(ImGui::Button("calc disk radius from text file")){
      struct Row { int idx, idA, idB, snap; };      
      std::vector<Row> rows;
      {
	std::ifstream fin(fname_input);
	if (!fin) { std::cerr << "cannot open " << fname_input << '\n'; return; }
						
	std::string line;
	Row r;
	while (std::getline(fin, line))
	  {
	    if (line.empty() || line[0] == '#')      // ← # 行はスキップ
	      continue;
							
	    std::istringstream iss(line);
	    if (iss >> r.idx >> r.idA >> r.idB >> r.snap)
	      rows.push_back(r);                   // 正しく読めた行だけ追加
	    else
	      std::cerr << "parse error: " << line << '\n';
	  }
      }
					
      bool flag_first0 = true;
      for (auto& r : rows){
	if(r.snap < 0)
	  continue;
						
	FILE *fp_out;
	if(flag_first0){
	  fp_out = std::fopen(fname_output, "w");
	  fprintf(fp_out, "#index idA idB t_disk\n");	  
	  flag_first0 = false; 
	}else{
	  fp_out = std::fopen(fname_output, "a");
	}
						
	double time_disk = -1., time_not_disk = -1.;
	char fname_evolution[255];
	snprintf(fname_evolution, sizeof(fname_evolution), "binary_evolution_%d.txt" ,r.idx);
						
	bool flag_first = true;
	double dist_disk=0., r_disk1=0., r_disk2=0.;
						
	int snap_init = r.snap;
	snap_init = static_cast<int>(r.snap / fileInfo->skipStep) * fileInfo->skipStep;
						
	int snap_disk = -1, snap_not_disk = snap_init;
						
	for (int i=0;i<100;i++) {
	  int snap = snap_init + fileInfo->skipStep * i;	  
	  fileInfo->loadNewSnapshot(snap, Part);
	  if(Part->particleBlock.particles.size() == 0)
	    continue;
							
	  double r1, r2;
	  float pos1[3], pos2[3];
	  bool flag_found_binary = true;
	  for(int i=0;i<2;i++){
	    int id;
	    float *pos;
	    double *r_disk;
	    if(i==0){
	      id = r.idA;
	      pos = pos1;
	      r_disk = &r1;
	    }else{
	      id = r.idB;
	      pos = pos2;
	      r_disk = &r2;
	    }
								
	    DiskRadiusFinder::Params param_disk0;
	    bool flag_found = false;
	    for(auto &p : Part->particleBlock.particles){
	      if(p.ID == id){
		if(p.type != 0){
		  param_disk0.mass = p.mass;
		  for(int k=0;k<3;k++){
		    param_disk0.center[k] = pos[k] = p.pos[k];
		    param_disk0.v_center[k] = p.vel[k];
		  }
		  flag_found = true;
		}else
		  break;
	      }
									
	      if(flag_found)
		break;
	    }

	    if (flag_found) {
	      param_disk0.G = Part->GravConst_internal;
	      param_disk0.max_shell = 100;
	      param_disk0.scale_fac = Part->originalMax / Part->desiredMax;
	      
	      DiskObject disk;
	      disk.color = glm::vec3(1.0f, 1.0f, 1.0f);
	      disk.opacity = render->disks.opacity;
	      disk.tag = "main_disk";
	      
	      if (diskFinder->compute(Part->particleBlock.particles, param_disk, disk)) 
		*r_disk = disk.radius;
	    }else
	      flag_found_binary = false;	      
	  }
							
	  if(flag_found_binary == false)
	    continue;
							
	  FILE *fp_evo;
	  if(flag_first){
	    fp_evo = std::fopen(fname_evolution, "w");
	    flag_first = false;
	    time_not_disk = Part->particleBlock.header.time;
	    snap_not_disk = snap;
	  }else
	    fp_evo = std::fopen(fname_evolution, "a");
							
	  if (!fp_evo) { std::cerr << "cannot open " << fname_output << '\n'; return; }
							
	  if (flag_first) {                       /* ← ① ヘッダは最初だけ */
	    std::fprintf(fp_out, "index ID1 ID2 snap n a b c\n");
	    flag_first = false;
	  }
							
	  double dist2 = (pos1[0] - pos2[0])*(pos1[0] - pos2[0]) + (pos1[1] - pos2[1])*(pos1[1] - pos2[1]) + (pos1[2] - pos2[2])*(pos1[2] - pos2[2]);
	  bool flag_disk = (sqrt(dist2) < r1 + r2?1:0);
	  double scale_fac = Part->originalMax / Part->desiredMax;
							
	  std::fprintf(fp_evo, "%d %g %g %g %g %d\n"
		       , snap, Part->particleBlock.header.time, sqrt(dist2)*scale_fac, r1*scale_fac, r2*scale_fac, static_cast<int>(flag_disk));
	  std::fclose(fp_evo);
							
	  if(flag_disk){
	    time_disk = Part->particleBlock.header.time;
	    dist_disk = sqrt(dist2) * scale_fac;
	    snap_disk = snap;
	    r_disk1 = r1 * scale_fac;
	    r_disk2 = r2 * scale_fac;	    
	    break;
	  }else{
	    time_not_disk = Part->particleBlock.header.time;
	    snap_not_disk = snap;
	  }
	}
						
	std::fprintf(fp_out, "%d %d %d %g %d %g %g %g %g %d\n", r.idx, r.idA, r.idB, time_disk, snap_disk, dist_disk, r_disk1, r_disk2, time_not_disk, snap_not_disk);
	std::fclose(fp_out);
      }
    }
    break;
  }
			
  case ANALYSIS_ISO_DENSITY: {
    static int queryID1=0, queryID2=0;
    ImGui::InputInt("Particle ID1", &queryID1);
    ImGui::InputInt("Particle ID2", &queryID2); 
    ImGui::SliderFloat("Opacity##contour_ellipse", &render->ellipsoids.opacity, 0.0f, 1.0f); 
				
    if (ImGui::Button("Fit Iso-density ellipsoid")) {
      scene->ellipsoid.clearGroup("analysis_ellipsoid");
      
      EllipsoidObject obj;
      if (ellipsoid->computeEllipse(Part->particleBlock.particles, queryID1, queryID2, obj)) {
	obj.opacity = render->ellipsoids.opacity;
	obj.color = glm::vec3(1.0f);
	obj.tag = "analysis_ellipsoid";
	obj.renderMode = EllipsoidRenderMode::Solid;
	
	scene->ellipsoid.add(obj);
      }      
    }
				
    if (ImGui::Button("disable Ellipsoid")) {
      scene->ellipsoid.clearGroup("analysis_ellipsoid");
    }
				
    static char fname_input[255]="binary_fragmentation.txt";
    static char fname_output[255]="binary_fragmentation_output.txt";
    ImGui::InputText("Read target from text file", fname_input, IM_ARRAYSIZE(fname_input));
    ImGui::InputText("Output target from text file", fname_output, IM_ARRAYSIZE(fname_output));
				
    if(ImGui::Button("ellipsoidal fit from text file")){
      struct Row { int idx, idA, idB, snap; };      
      std::vector<Row> rows;
      {
	std::ifstream fin(fname_input);
	if (!fin) { std::cerr << "cannot open " << fname_input << '\n'; return; }
						
	std::string line;
	Row r;
	while (std::getline(fin, line))
	  {
	    if (line.empty() || line[0] == '#')      // ← # 行はスキップ
	      continue;
							
	    std::istringstream iss(line);
	    if (iss >> r.idx >> r.idA >> r.idB >> r.snap)
	      rows.push_back(r);                   // 正しく読めた行だけ追加
	    else
	      std::cerr << "parse error: " << line << '\n';
	  }
      }
					
      bool flag_first = true;
      for (auto& r : rows){
	if(r.snap < 0)
	  continue;
						
	fileInfo->loadNewSnapshot(r.snap, Part);
	if(Part->particleBlock.particles.size() == 0)
	  continue;        

	EllipsoidObject obj;
	bool flag_ellipse = ellipsoid->computeEllipse(Part->particleBlock.particles, r.idA, r.idB, obj);
	if(flag_ellipse == false)
	  continue;
	
	FILE *fp_out;
	if(flag_first)
	  fp_out = std::fopen(fname_output, "a");
	else
	  fp_out = std::fopen(fname_output, "a");
						
	if (!fp_out) { std::cerr << "cannot open " << fname_output << '\n'; return; }
						
	if (flag_first) {                       /* ← ① ヘッダは最初だけ */
	  std::fprintf(fp_out, "index ID1 ID2 snap n a b c\n");
	  flag_first = false;
	}
	
	double a = obj.radii.x;
	double b = obj.radii.y;
	double c = obj.radii.z;
	double n = ellipsoid->getDensityThreshold();	
	std::fprintf(fp_out, "%d %d %d %d %g %g %g %g\n", r.idx, r.idA, r.idB, r.snap, n, a, b, c);
	std::fclose(fp_out);
      }
					
    }
    break;
  }
#endif      
  }
}


static void DrawRenderingSection(SettingsUIContext& ctx, SettingsRuntimeState& rt){
  if (!ImGui::CollapsingHeader("Rendering"))
    return;

  ParticleArray* Part = ctx.P;
  CameraContext& camCtx = *ctx.camCtx;
  FileInfo* fileInfo = ctx.fileInfo;
  auto& particleVisual = *ctx.particleVisual;

  auto* services = ctx.services;
  auto* projectionMap2D = services->projectionMap2D.get();
#ifdef STREAM_LINE
  auto* streamLine      = services->streamLine.get();
#endif
#ifdef VOLUME_RENDERING
  auto* bvh = services->bvh.get();
  auto* tf  = services->tf.get();
  auto& volume = services->volume;
#endif
  auto* render = ctx.render;
  auto* scene = ctx.scene;
#ifdef ISO_CONTOUR
  auto* isoContour = ctx.isoContour;
#endif
  
  enum RenderingMode {
    RENDER_PROJECTION_MAP,
    RENDER_STREAM_LINE,
    RENDER_ISO_CONTOUR,
    RENDER_VOLUME_RENDERING,
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
    { "volume rendering", RENDER_VOLUME_RENDERING },
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
      OpenProjectionMapUI();    
				
    DrawProjectionMapUI(*projectionMap2D, Part, camCtx, render->cuboidAnnotations, fileInfo->currentFileIndex);
				
    ImGui::Text("create projection maps for continuous snapshots");
				
    static int nsnapshots = 10;
    static char outputFileFormat[255]="image_%04d.png";
    static char outputFolderPath[255]="./output";
    static char outputFileName[255]="output.mp4";
    ImGui::InputInt("number of snapshots##render", &nsnapshots);
    ImGui::InputText("Output File Format##render", outputFileFormat, IM_ARRAYSIZE(outputFileFormat));
    ImGui::InputText("Output Folder##render", outputFolderPath, IM_ARRAYSIZE(outputFolderPath));
    ImGui::InputText("Output Name of Movie##render", outputFileName, IM_ARRAYSIZE(outputFolderPath));
				
    static bool flagFaceOn = false;
    ImGui::Checkbox("show face-on view", &flagFaceOn);
				
    static bool flagSinkCenter = false, flagSinkCenterMassive = false, flagMassCenter = false;
    static int particleID_center = 0;
    static float rcrit_for_MassCenter = 0., ncrit_for_MassCenter = 0.;
    ImGui::Checkbox("follow the center around the particle", &flagSinkCenter);
    if(flagSinkCenter){
      ImGui::Checkbox("the most massive sink particle", &flagSinkCenterMassive);
      if(flagSinkCenterMassive == false)
	ImGui::InputInt("particle ID", &particleID_center);	
					
      ImGui::Checkbox("mass center around the particle", &flagMassCenter);
      if(flagMassCenter){
	ImGui::InputFloat("distance from the particle", &rcrit_for_MassCenter);
	ImGui::InputFloat("the minimum density", &ncrit_for_MassCenter);
      }
    }
				
    if(ImGui::Button("generate maps")){
      int savedStep = fileInfo->currentStep;
					
      namespace fs = std::filesystem;
      const fs::path dir = "ffmpeg_frames";
					
      try {
	auto ensure_dir = [](const fs::path& p) {
	  if (fs::exists(p)) {
	    if (!fs::is_directory(p)) {
	      throw fs::filesystem_error("Path exists but is not a directory", p,
					 std::make_error_code(std::errc::not_a_directory));
	    }
	  } else {
	    fs::create_directories(p);
	  }
	};
						
	ensure_dir(dir);
	ensure_dir(outputFolderPath);
						
	if (!fs::exists(dir)) {
	  fs::create_directory(dir);
	  std::cout << "Directory created: " << dir << std::endl;
	}
						
	if (!fs::exists(outputFolderPath)) {
	  fs::create_directory(outputFolderPath);
	  std::cout << "Directory created: " << outputFolderPath << std::endl;
	}
						
	int count_i = 0;
	for(int i=0;i<nsnapshots;i++){
	  fileInfo->currentStep = savedStep;
	  if(i > 0) fileInfo->currentStep += i;
							
	  int newFileIndex = fileInfo->initialIndex + fileInfo->currentStep * fileInfo->skipStep;
	  fileInfo->loadNewSnapshot(newFileIndex, Part);            
							
	  if(Part->particleBlock.particles.size() == 0)
	    continue;
							
	  char filename_format[512];
	  snprintf(filename_format, sizeof(filename_format), "%s/%s", outputFolderPath, outputFileFormat);
							
	  char filename[512];
	  snprintf(filename, sizeof(filename), filename_format, newFileIndex);
							
	  int flag_use_amvector = 0;
	  if(i==0 && flagFaceOn)
	    flag_use_amvector = 1;
							
	  int flag_center = 0;
#ifdef CLUMP_DATA_READ
	  if(Part->flag_follow_clump_center)
	    flag_center = 1;
#endif
	  if(Part->flag_follow_particle_ID)
	    flag_center = 1;
							
	  // まず、カメラターゲットを pos_center 配列に格納しておく
	  float pos_center[3] = {
	    camCtx.cameraTarget[0],
	    camCtx.cameraTarget[1],
	    camCtx.cameraTarget[2]
	  };
							
	  if(flagSinkCenter){
	    double pos_init[3];
	    bool flag_found = false;
	    if(flagSinkCenterMassive == false){
	      for(auto &p : Part->particleBlock.particles){
		if(p.ID == particleID_center){
		  pos_init[0] = p.pos[0];
		  pos_init[1] = p.pos[1];
		  pos_init[2] = p.pos[2];
		  flag_found = true;
		}
		if(flag_found)
		  break;
	      }
	    }
								
	    if(flagSinkCenterMassive || (flag_found == false)){
	      double mass_max = 0.;
	      for(auto &p : Part->particleBlock.particles){
		if(p.type < 3)
		  continue;
										
		if(mass_max < p.mass){
		  pos_init[0] = p.pos[0];
		  pos_init[1] = p.pos[1];
		  pos_init[2] = p.pos[2];
		  flag_found = true;
		  mass_max = p.mass;
		}
	      }
	    }
								
	    if(flag_found){
	      pos_center[0] = pos_init[0];
	      pos_center[1] = pos_init[1];
	      pos_center[2] = pos_init[2];
	      flag_center = 1;
	    }
								
	    if(flag_found && flagMassCenter){
	      double pos_temp[3] = {0.,0.,0.}, weight = 0.;
	      for(auto &p : Part->particleBlock.particles){
		if(p.type == 1 || p.type == 2)
		  continue;
										
		if(p.type == 0 && p.density < ncrit_for_MassCenter)
		  continue;
										
		double dist2 =
		  (pos_init[0] - p.pos[0])*(pos_init[0] - p.pos[0])
		  + (pos_init[1] - p.pos[1])*(pos_init[1] - p.pos[1])
		  + (pos_init[2] - p.pos[2])*(pos_init[2] - p.pos[2]);
										
		if(dist2 > rcrit_for_MassCenter * rcrit_for_MassCenter)
		  continue;
										
		double mass = p.mass;
		pos_temp[0] += mass * p.pos[0];
		pos_temp[1] += mass * p.pos[1];
		pos_temp[2] += mass * p.pos[2];
		weight += mass;
	      }
									
	      pos_center[0] = pos_temp[0] / weight;
	      pos_center[1] = pos_temp[1] / weight;
	      pos_center[2] = pos_temp[2] / weight;
	      flag_center = 1;
	    }
	  }
	  
	  projectionMap2D->set_projection_parameters(Part->particleBlock.particles, flag_use_amvector, flag_center ? pos_center : nullptr, -1.0f,
						     std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN(), -1, -1, "");
							
	  projectionMap2D->make_density_map(Part, filename);
							
	  char linkname[512];
	  snprintf(linkname, sizeof(linkname), "ffmpeg_frames/frame_%04d.png", count_i);
	  count_i++;
							
	  std::filesystem::remove(linkname);
	  std::filesystem::create_symlink(std::filesystem::absolute(filename), linkname);
	}
						
	// ffmpeg を呼び出す（mp4 形式、30fps）
	std::string ffmpegCommand =
	  "ffmpeg -y -framerate 30 -i ffmpeg_frames/frame_%04d.png -vf \"scale=ceil(iw/2)*2:ceil(ih/2)*2\" -c:v libx264 -pix_fmt yuv420p " + std::string(outputFolderPath) + "/" + std::string(outputFileName);
	std::system(ffmpegCommand.c_str());
	fs::remove_all("ffmpeg_frames");
						
	fileInfo->currentStep = savedStep;
	fileInfo->currentFileIndex = fileInfo->initialIndex + fileInfo->currentStep * fileInfo->skipStep;    	
      } catch (const fs::filesystem_error& e) {
	std::cerr << "Error creating directory: " << e.what() << std::endl;
      }
    }
    break;
  }
			
#ifdef STREAM_LINE
  case RENDER_STREAM_LINE: {
    static int n_seeds=1;
    ImGui::Text("Seed setup");
    ImGui::InputInt("number of seed points", &n_seeds);
				
    static float seed_center[3] = {0.,0.,0.}, seed_len[3] = {100.,100.,100.}, seed_opacity = 0.1;
    bool seedRegionDirty = false;
				
    if (ImGui::InputFloat3("Center of the region to place seed points", seed_center, "%.3f")){
      seedRegionDirty = true;
    }
    
    // 2) Side‐length: rebuild when changed
    if (ImGui::InputFloat3("side len", seed_len, "%.3f")) {
      seedRegionDirty = true;
    }
				
    // 3) Opacity: rebuild when changed
    if (ImGui::SliderFloat("opacity##cubic", &seed_opacity, 0.f, 1.f, "%.2f")) {
      seedRegionDirty = true;
    }
				
    // 4) If either length or opacity changed, re‐create exactly one cube
    if (seedRegionDirty) {
      UpdateSeedRegionPreview(*streamLine, scene->cube, *render, seed_center, seed_len, seed_opacity);
      seedRegionDirty = false;
    }
				
    static bool flag_limit_stream_region = false;
    static float sl_center[3]={0.,0.,0.}, sl_len[3]={0.,0.,0.};
				
    ImGui::Text("Stream line setting");    
    ImGui::Checkbox("limit stream lines in box", &flag_limit_stream_region);
    if(flag_limit_stream_region){
      bool flag_reset_region = false;
      if(ImGui::InputFloat3("center of stream line region", sl_center, "%.3f")){
	flag_reset_region = true;
      }
					
      if(ImGui::InputFloat3("side len##stream line", sl_len, "%.3f")){
	flag_reset_region = true;
      }
					
      if(flag_reset_region){
	if(sl_len[0] > 0. && sl_len[1] > 0. && sl_len[2] > 0.){
	  streamLine->setStreamRegionByHand(sl_center, sl_len);
	}else{
	  streamLine->disableStreamRegion();
	}
      }
    }else
      streamLine->disableStreamRegion();

    if (ImGui::Button("Build stream lines")) {
      streamLine->setRegionFromParticleData(Part->particleBlock.particles);
      streamLine->setStreamRegionFromParticleData(Part->particleBlock.particles);

      streamLine->setSeeds(Part->particleBlock.particles, n_seeds);
      float degree = 10.f;

      scene->line.clearGroup("streamline");

      auto lines = streamLine->build(Part->particleBlock,  degree);

      for (auto& line : lines) {
	line.color = glm::vec3(1.0f, 1.0f, 1.0f);
	line.opacity = 1.0;
	line.tag = "streamline";
	scene->line.add(line);
      }
    }

    if (ImGui::Button("disable Grid & Mesh")) {
      scene->line.clearGroup("streamline");
    }
				
    break;
  }
#endif
			
#ifdef ISO_CONTOUR
  case RENDER_ISO_CONTOUR: {
    static float isoLevel = 0.;
				
    ImGui::InputFloat("Threshold value for iso-contour", &isoLevel);
    ImGui::SliderFloat("Opacity", &render->isocontour.opacity, 0.0f, 1.0f);
				
    static int max_treelevel = 15;
    ImGui::SliderInt("Maximum level of OctTree", &max_treelevel, 5, 20);
				
    static QuantityId selectedVar_iso = QuantityId::Density;
    if (ImGui::BeginCombo("Quantity for Iso-Contour", QuantityLabel(selectedVar_iso))) {
      for (int q = 0; q < Part->particleBlock.nUIQ; ++q) {
	QuantityId cand = Part->particleBlock.uiQ[q];
	bool is_selected = (cand == selectedVar_iso);
	if (ImGui::Selectable(QuantityLabel(cand), is_selected)) selectedVar_iso = cand;
	if (is_selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
				
    if (ImGui::Button("Build OctTree & Mesh")) {
      BuildIsoContourGeometry(*Part,
			      selectedVar_iso,
			      isoLevel,
			      max_treelevel,
			      *isoContour);

      render->isocontour.show = true;
      render->isocontour.cpuUpdated = true;
    }
								
    if (ImGui::Button("disable Grid & Mesh")) {
      render->isocontour.show = false;
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

static void DrawOtherSettingsSection(ParticleArray* Part, FileInfo* fileInfo, SettingsRuntimeState& rt, RenderRuntimeState& render){
  if(!ImGui::CollapsingHeader("Other settings"))
    return;
  
  bool unitChanged = false;
  if(ImGui::CollapsingHeader("Units")){
    unitChanged |= ImGui::InputDouble("UnitLength_in_cm"   , &Part->UnitLength_in_cm   , 0.,0., "%g");
    unitChanged |= ImGui::InputDouble("UnitMass_in_msun"   , &Part->UnitMass_in_g      , 0.,0., "%g");
    unitChanged |= ImGui::InputDouble("UnitVelocity_in_cm_per_s", &Part->UnitVelocity_in_cm_per_s, 0.,0., "%g");
    unitChanged |= ImGui::InputDouble("Hubble"             , &Part->Hubble, 0.,0., "%g");
    unitChanged |= ImGui::Checkbox("ComovingCorrdinate", &Part->useComovingCorrdinate);
			
    ImGui::SeparatorText("Presets");      
    if (ImGui::Button("AU"))   { Part->UnitLength_in_cm = Part->au_in_cm;      unitChanged = true; }
    ImGui::SameLine();
    if (ImGui::Button("pc"))   { Part->UnitLength_in_cm = Part->pc_in_cm;      unitChanged = true; }
    ImGui::SameLine();
    if (ImGui::Button("kpc"))  { Part->UnitLength_in_cm = Part->kpc_in_cm;     unitChanged = true; }
    ImGui::SameLine();
    if (ImGui::Button("Mpc"))  { Part->UnitLength_in_cm = Part->Mpc_in_cm;     unitChanged = true; }
			
    if (ImGui::Button("Msun"))   { Part->UnitMass_in_g   = Part->msolar_in_g;    unitChanged = true; }
    ImGui::SameLine();
    if (ImGui::Button("1e10 Msun")){ Part->UnitMass_in_g   = 1.e10*Part->msolar_in_g; unitChanged = true; }
  }
		
  if(unitChanged){
    Part->setUnits();
    fileInfo->setUnit(Part);
  }
		
  if(ImGui::CollapsingHeader("Zoom Range")){
    ImGui::InputFloat("Min Zoom", &rt.minZoom, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("Max Zoom", &rt.maxZoom, 0.0f, 0.0f, "%g");
  }
		
  if(ImGui::CollapsingHeader("Cross Marker"))
    ImGui::SliderFloat("Cross Marker Size", &render.crossGizmo.size, 0.01f, 1.0f); 
}


