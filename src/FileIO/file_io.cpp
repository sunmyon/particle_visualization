#include "interaction/camera.h"
#include "FileIO/file_io.h"
#include "FileIO/hdf5_reader.h"
#include "FileIO/binary_reader.h"

#include <imgui.h>

#include <chrono>
#include <iomanip>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <future>
#include <thread>
#include <fstream>
#include <cstring>

#include "core/PerfTimer.h"

namespace {
  struct ReaderSelection {
    std::unique_ptr<IParticleReader> reader;
    std::vector<FieldSpec> format;
    std::string fullPath;
  };

  ReaderSelection makeReaderSelection(const FileInfo& info, int fileNumber) {
    ReaderSelection sel;

    char fileName[512];
    std::snprintf(fileName, sizeof(fileName), info.fileFormat, fileNumber);
    sel.fullPath = std::string(info.folderPath) + fileName;

    std::string ext;
    auto pos = sel.fullPath.find_last_of('.');
    if (pos != std::string::npos) {
      ext = sel.fullPath.substr(pos);
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    switch (info.getFormatMode()) {
    case FileFormat::Auto:
#ifdef HAVE_HDF5
      if (ext == ".h5" || ext == ".hdf5") {
        sel.reader = std::make_unique<HDF5Reader>();
        sel.format = info.formatTokens_hdf5;
        break;
      }
#endif
#ifdef USE_MMAP
      sel.reader = std::make_unique<MMapReader>();
#else
      sel.reader = std::make_unique<BinaryReader>();
#endif
      sel.format = info.formatTokens;
      break;

#ifdef HAVE_HDF5
    case FileFormat::HDF5:
      sel.reader = std::make_unique<HDF5Reader>();
      sel.format = info.formatTokens_hdf5;
      break;
#endif

    case FileFormat::Binary:
#ifdef USE_MMAP
      sel.reader = std::make_unique<MMapReader>();
#else
      sel.reader = std::make_unique<BinaryReader>();
#endif
      sel.format = info.formatTokens;
      break;

    case FileFormat::Gadget:
      break;

    case FileFormat::Framed:
      break;

    default:
      break;
    }

    return sel;
  }
}

TrackingVector<int> FileInfo::getStarParticleID(int indexFile){
  ParticleBlock p_block;
  loadSingleFile(indexFile, p_block);

  TrackingVector<int> IDs;
  
  for(auto &p : p_block.particles){
    if(p.type < 3)
      continue;
    IDs.push_back(p.ID);
  }

  return IDs;
}

void FileInfo::generateTestData(ParticleArray *P){
  ParticleBlock block = ParticleBlock::makeTestParticleBlock();
  P->setParticleBlock(std::move(block), nullptr);
}

void FileInfo::loadNewSnapshot(int newFileIndex, ParticleArray *P){
  TIME_FUNCTION();
  
  bool foundInCache = false;
  ParticleBlock block;
  {
    std::lock_guard<std::mutex> lock(g_dataMutex);
    if (prefetchCache.pop(newFileIndex, block)) {
      ParticleBlock oldBlock;
      P->setParticleBlock(std::move(block), &oldBlock);
      foundInCache = true;
    }    
  }

  if (!foundInCache) {
    if (!isLoading)
      loadBatch(newFileIndex, batchSize, skipStep, P);
  }
  
  snapshotUpdated = true;
  currentFileIndex = newFileIndex;
  
  printf("currentStep=%d newFileIndex=%d\n", currentStep, newFileIndex);
  
#ifdef CLUMP_DATA_READ
  P->flag_renew_clumpList = true;
#endif
}

bool FileInfo::syncLoadFirstFile(int targetFile, ParticleArray *P) {
  ParticleBlock newBlock;

  if (!loadSingleFile(targetFile, newBlock)) {
    P->particleBlock.clear();
    std::cerr << "Failed to load first file: " << targetFile << std::endl;
    return false;
  }

  ParticleBlock oldBlock;
  P->setParticleBlock(std::move(newBlock), &oldBlock);

  std::lock_guard<std::mutex> lock(g_dataMutex);
  prefetchCache.clear();

  return true;
}

void FileInfo::asyncLoadRemainingFiles(int targetFile, int batchSize, int skipStep, int generation) {
  TrackingVector<std::pair<int, ParticleBlock>> loaded;

  for (int i = 1; i < batchSize; ++i) {
    int fileNumber = targetFile + i * skipStep;
    ParticleBlock block;
    
    if (loadSingleFile(fileNumber, block)) {
      loaded.push_back({fileNumber, std::move(block)});
    }
  }

  {
    std::lock_guard<std::mutex> lock(g_dataMutex);
    if (prefetchGeneration.load() != generation) return;
    for (auto& e : loaded) {
      prefetchCache.push(e.first, std::move(e.second));
    }
  }
}

void FileInfo::loadBatch(int targetFile, int batchSize, int skipStep, ParticleArray *P) {
  const int gen = ++prefetchGeneration;
  prefetchRunning = true;
  isLoading = true;

  if (!syncLoadFirstFile(targetFile, P)) {
    prefetchRunning = false;
    isLoading = false;
    return;
  }
  
  prefetchFuture = std::async(std::launch::async,
                              [this, targetFile, batchSize, skipStep, gen]() {
                                asyncLoadRemainingFiles(targetFile, batchSize, skipStep, gen);
                                if (prefetchGeneration.load() == gen) {
                                  prefetchRunning = false;
                                  isLoading = false;
                                }
                              });
}

void FileInfo::initDefaultFormatTokens() {
    formatTokens.clear();
    FieldSpec token;
    token.key = FieldKey::Position; token.type = DataType::Float; token.count = 3;
    formatTokens.push_back(token);
    token.key = FieldKey::Velocity; token.type = DataType::Float; token.count = 3;
    formatTokens.push_back(token);
    token.key = FieldKey::Type; token.type = DataType::Int32; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::ID; token.type = DataType::Int32; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::Hsml; token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::Density; token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::Temperature; token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::Dummy; token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::Value; token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::Value2; token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.key = FieldKey::Dummy; token.type = DataType::Float; token.count = 4;
    formatTokens.push_back(token);
    token.key = FieldKey::Mass; token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);

    formatTokens_hdf5 = formatTokens;
} 

// ------------------------------
// データフォーマット編集ダイアログ
// ------------------------------
void FileInfo::DrawFormatDialog() {
  if (!showFormatDialog) return;
  
  ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);
    
  if(!ImGui::Begin("Edit Data Format", &showFormatDialog)){
    ImGui::End();
    return;
  }

  for (size_t i = 0; i < formatTokensEdit.size(); i++) {
    ImGui::PushID(i);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s, %s, count=%d",
		  GetFieldKeyDisplayName(formatTokensEdit[i].key),
		  GetDataTypeDisplayName(formatTokensEdit[i].type),
		  formatTokensEdit[i].count);

    // リスト内の項目として表示（Selectable を使う）
    if (ImGui::Selectable(buf))
      {
	// ※選択時の処理（必要なら）
      }

    // ここからドラッグ＆ドロップのソース処理
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
      {
	// 現在の項目インデックスを payload として送る
	int payloadIndex = static_cast<int>(i);
	ImGui::SetDragDropPayload("DND_FORMAT_TOKEN", &payloadIndex, sizeof(payloadIndex));

	ImGui::Text("Moving %s", buf);
	ImGui::EndDragDropSource();
      }

    // --- ドロップ先としての処理 ---
    if (ImGui::BeginDragDropTarget())
      {
	// ※ここでは、ドロップ位置を細かく判定するために、現在の項目の矩形を利用します
	ImVec2 itemMin = ImGui::GetItemRectMin();
	ImVec2 itemMax = ImGui::GetItemRectMax();
	float midY = (itemMax.y + itemMin.y) * 0.5;

	const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_FORMAT_TOKEN");
	    
	if (payload)
	  {
	    IM_ASSERT(payload->DataSize == sizeof(int));
	    int srcIndex = *(const int*)payload->Data;
	    if(static_cast<size_t>(srcIndex) > i)
	      midY = itemMax.y;
	    else
	      midY = itemMin.y;
	  }

	// マウス位置と項目の中央を比較して、挿入すべき位置を決める
	int dropIndex = i;  // 基本は「この項目の前」に挿入
	if (ImGui::GetIO().MousePos.y > midY)
	  dropIndex = i + 1;  // 項目の下側なら「この項目の後ろ」に挿入

	if (payload)
	  {
	    IM_ASSERT(payload->DataSize == sizeof(int));
	    int srcIndex = *(const int*)payload->Data;
	    if (srcIndex != dropIndex && srcIndex != dropIndex - 1)
	      {
		// 取り出し
		FieldSpec token = formatTokensEdit[srcIndex];
		formatTokensEdit.erase(formatTokensEdit.begin() + srcIndex);
		// 調整：削除前のインデックスが dropIndex より小さい場合は、dropIndex が1つ減る
		if (srcIndex < dropIndex)
		  dropIndex--;
		formatTokensEdit.insert(formatTokensEdit.begin() + dropIndex, token);
	      }
	  }
	ImGui::EndDragDropTarget();
      }

    if (ImGui::BeginCombo("##fieldKeyCombo",
			  GetFieldKeyDisplayName(formatTokensEdit[i].key))) {
      for (int n = 0; n < kNumAvailableFieldKeys; ++n) {
	FieldKey key = kAvailableFieldKeys[n];
	bool is_selected = (formatTokensEdit[i].key == key);

	if (ImGui::Selectable(GetFieldKeyDisplayName(key), is_selected)) {
	  formatTokensEdit[i].key = key;
	  ApplyDefaultFieldSpec(formatTokensEdit[i]);
	}
	if (is_selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    if (ImGui::BeginCombo("Type", GetDataTypeDisplayName(formatTokensEdit[i].type))) {
      for (int n = 0; n < kNumDataTypeChoices; ++n) {
	DataType type = kDataTypeChoices[n].type;
	bool is_selected = (formatTokensEdit[i].type == type);

	if (ImGui::Selectable(kDataTypeChoices[n].name, is_selected)) {
	  formatTokensEdit[i].type = type;
	}
	if (is_selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }

    ImGui::InputInt("Count", &formatTokensEdit[i].count);
    if (formatTokensEdit[i].count < 0)
      formatTokensEdit[i].count = 0;
    
    if (ImGui::Button("Delete")) {
      formatTokensEdit.erase(formatTokensEdit.begin() + i);
      ImGui::PopID();
      i--;
      continue;
    }
    ImGui::Separator();
    ImGui::PopID();
  }
    
  if (ImGui::Button("Add Token")) {
    FieldSpec newToken;
    newToken.key = FieldKey::Dummy;
    ApplyDefaultFieldSpec(newToken);
    formatTokensEdit.push_back(newToken);
  }
  if (ImGui::Button("OK")) {
    formatTokens = formatTokensEdit;
    showFormatDialog = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    showFormatDialog = false;
  }
  ImGui::End();
}

#ifdef HAVE_HDF5
void FileInfo::ShowHDF5FieldMappingDialog() {
  if (!showHDF5MappingDialog) return;
  
  ImGui::SetNextWindowSize(ImVec2(300, 300), ImGuiCond_FirstUseEver);

  if(!ImGui::Begin("Edit HDF5 Data Format", &showHDF5MappingDialog)){
    ImGui::End();
    return;
  }

  if (ImGui::BeginTable("FieldTable", 6,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
    ImGui::TableSetupColumn("##", ImGuiTableColumnFlags_WidthFixed, 20.0f);
    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("dataType", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("count", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableHeadersRow();

    for (int i = 0; i < (int)formatTokensEdit.size(); ++i) {
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();

      // 行番号（または選択用アイコンなど）
      ImGui::Text("%d", i + 1);

      // Label 列：ドロップダウン＋自由入力
      ImGui::TableNextColumn();
      if (ImGui::BeginCombo("##fieldKeyCombo",
                            GetFieldKeyDisplayName(formatTokensEdit[i].key))) {
        for (int n = 0; n < kNumAvailableFieldKeys; ++n) {
          FieldKey key = kAvailableFieldKeys[n];
          bool is_selected = (formatTokensEdit[i].key == key);

          if (ImGui::Selectable(GetFieldKeyDisplayName(key), is_selected)) {
            formatTokensEdit[i].key = key;
            ApplyDefaultFieldSpec(formatTokensEdit[i]);
          }
          if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      {
	ImGui::PushItemWidth(-1);
	char buf[128];
	std::snprintf(buf, sizeof(buf), "%s", formatTokensEdit[i].sourceName.c_str());
      
	if (ImGui::InputText("##sourceName", buf, IM_ARRAYSIZE(buf))) {
	  formatTokensEdit[i].sourceName = buf;
	}
      
	ImGui::PopItemWidth();
      }
            
      ImGui::TableNextColumn();
      if (ImGui::BeginCombo("##typeCombo",
                            GetDataTypeDisplayName(formatTokensEdit[i].type))) {
        for (int n = 0; n < kNumDataTypeChoices; ++n) {
          DataType type = kDataTypeChoices[n].type;
          bool is_selected = (formatTokensEdit[i].type == type);

          if (ImGui::Selectable(kDataTypeChoices[n].name, is_selected)) {
            formatTokensEdit[i].type = type;
          }
          if (is_selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }

      ImGui::TableNextColumn();
      ImGui::InputInt("##count", &formatTokensEdit[i].count, 1, 10);
      if (formatTokensEdit[i].count < 1) formatTokensEdit[i].count = 1;

      ImGui::TableNextColumn();
      if (ImGui::Button("-", ImVec2(24,24))) {
	formatTokensEdit.erase(formatTokensEdit.begin() + i);
	ImGui::PopID();
	--i;
	continue;
      }

      ImGui::PopID();
    }

    ImGui::EndTable();
  }

  // フィールド追加ボタン
  if (ImGui::Button("Add field")) {
    FieldSpec newToken;
    newToken.key = FieldKey::Dummy;
    ApplyDefaultFieldSpec(newToken);
    formatTokensEdit.push_back(newToken);
  }
  ImGui::SameLine();
  // 確定・キャンセル
  if (ImGui::Button("OK")) {
    formatTokens_hdf5 = formatTokensEdit;
    showHDF5MappingDialog = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    showHDF5MappingDialog = false;
  }
    
  ImGui::End();
}
#endif

bool FileInfo::loadSingleFile(int fileNumber, ParticleBlock& outBlock) {
  TIME_FUNCTION();

  outBlock.header.UnitLength_in_cm = units.length_cm;
  outBlock.header.UnitMass_in_g = units.mass_g;
  outBlock.header.UnitVelocity_in_cm_per_s = units.velocity_cm_per_s;
  outBlock.header.HubbleParam = units.hubble;

  ReaderSelection sel = makeReaderSelection(*this, fileNumber);
  if (!sel.reader) {
    std::cerr << "Failed to select reader for file #" << fileNumber << "\n";
    return false;
  }

  if (!sel.reader->tryFixAndCheckBinary(sel.fullPath, outBlock.header, sel.format)) {
    std::cerr << "the format is incorrect\n";
    return false;
  }

  if (!sel.reader->open(sel.fullPath, outBlock.header)) {
    std::cerr << "failed to open the file: " << sel.fullPath << "\n";
    return false;
  }
    
  {
    TIME_SCOPE("parse header");

    bool ok = false;
    if (enableMask) {
      ParticleMask pmask{currentMaskConfig};
      ok = sel.reader->readRange(outBlock, 0, sel.reader->particleCount(),
				 sel.format, &pmask);
    }

    if (!ok) {
      ok = sel.reader->readAll(outBlock, sel.format);
    }

    sel.reader->close();    
    if (!ok) {
      std::cerr << "Failed to read particle data: " << sel.fullPath << "\n";
      return false;
    }
  }
  	
  return true;
}
