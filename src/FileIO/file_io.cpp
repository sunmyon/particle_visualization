#include "interaction/camera.h"
#include "FileIO/file_io.h"
#include "FileIO/hdf5_reader.h"
#include "FileIO/binary_reader.h"

#include <imgui.h>
#include "implot.h"

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
  
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(g_dataMutex);
    for (auto it = prefetchCache.begin(); it != prefetchCache.end(); ++it) {
      if (it->fileIndex == newFileIndex) {
        ParticleBlock oldBlock;
        P->setParticleBlock(std::move(it->block), &oldBlock);
        prefetchCache.erase(it);

        found = true;
        break;
      }
    }
  }

  if (!found) {
    if (!isLoading)
      loadBatch(newFileIndex, batchSize, skipStep, P);
  }
  
  snapshotUpdated = true;
  currentFileIndex = newFileIndex;
  
  printf("currentStep=%d newFileIndex=%d currentBatchStart=%d\n", currentStep, newFileIndex, currentBatchStart);
  
#ifdef CLUMP_DATA_READ
  P->flag_renew_clumpList = true;
#endif
}


void FileInfo::syncLoadFirstFile(int targetFile, ParticleArray *P) {
  ParticleBlock newBlock;

  if (!loadSingleFile(targetFile, newBlock)) {
    P->particleBlock.clear();
    std::cerr << "Failed to load first file: " << targetFile << std::endl;
    return;
  }

  ParticleBlock oldBlock;
  const bool hadOld = P->setParticleBlock(std::move(newBlock), &oldBlock);
  std::lock_guard<std::mutex> lock(g_dataMutex);

  prefetchCache.clear();
}

void FileInfo::asyncLoadRemainingFiles(int targetFile, int batchSize, int skipStep) {
  TrackingVector<PrefetchEntry> loaded;

  for (int i = 1; i < batchSize; ++i) {
    int fileNumber = targetFile + i * skipStep;

    ParticleBlock block;
    if (loadSingleFile(fileNumber, block)) {
      PrefetchEntry e;
      e.fileIndex = fileNumber;
      e.block = std::move(block);
      loaded.push_back(std::move(e));
    }
  }

  {
    std::lock_guard<std::mutex> lock(g_dataMutex);
    for (auto& e : loaded) {
      prefetchCache.push_back(std::move(e));
    }
  }
}


void FileInfo::loadBatch(int targetFile, int batchSize, int skipStep, ParticleArray *P) {
  isLoading = true;

  syncLoadFirstFile(targetFile, P);
  currentBatchStart = targetFile;  // 新たなバッチの開始位置
    
  std::future<void> asyncLoader = std::async(std::launch::async,
					     &FileInfo::asyncLoadRemainingFiles, this,
					     targetFile, batchSize, skipStep);

  isLoading = false;
}


#ifdef CLUMP_DATA_READ
#include "FindClumps/find_clumps_IO.h"
int ParticleArray::readClumpData(int snapshotIndex){
  uint32_t mask = (L_TIME | L_CLUMP_ID | L_CLUMP_NEXT_ID | L_CLUMP_SIZE | L_CLUMP_OFFSET
		   | L_CLUMP_STELLAR_COUNT | L_CLUMP_STELLAR_ID | L_CLUMP_STELLAR_MASS
		   | L_CLUMP_POSITION | L_CLUMP_DENSITY
		   | L_CLUMP_TEMPERATURE | L_CLUMP_MASS
		   | L_PARTICLE_IDS);
  ClumpInfoIO in;
  
  bool flag = ClumpIO::readSnapshot(fname_clump_file, snapshotIndex, mask, in);
  if(flag == false)
    return 1;

  size_t nClumps = in.clump_id.size();
  Clumps={};
  
  // 各 clump の情報を ClumpData にセットし、sorted_particle_ids から該当する粒子IDリストを抽出
  for (size_t i = 0; i < nClumps; i++) {
    ClumpData cd;
    cd.clumpID = in.clump_id[i];
    if(in.clump_next_id.size())
      cd.nextClumpID = in.clump_next_id[i];

    cd.count = in.clump_size[i];
    cd.offset = in.clump_offset[i];

    for(int k=0;k<3;k++){
      cd.originalPos[k] = in.clump_position[i * 3 + k];
      cd.Pos[k] = cd.originalPos[k] * desiredMax / originalMax;
    }
       
    cd.density = in.clump_density[i];
    cd.temperature = in.clump_temperature[i];    
    cd.mass = in.clump_mass[i];

    cd.stellar_mass = in.clump_stellar_mass[i];
    cd.stellar_count = in.clump_stellar_count[i];
    cd.stellar_id = in.clump_stellar_id[i];
    
    // 各クラスタに属する粒子IDを取得: offset と size を使って sorted_particle_ids から切り出し
    int off = in.clump_offset[i];
    int sz = in.clump_size[i];
    for (int j = 0; j < sz; j++) {
      if (off + j < static_cast<int>(in.particle_ids.size()))
	cd.IDs.push_back(in.particle_ids[off + j]);
      else
	throw std::runtime_error("Invalid offset/size: exceeds sorted_particle_ids size");
    }

    Clumps.push_back(cd);
  }

  return 0;
}
#endif

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
  
  // 1) フルパスを組み立て
  char fileName[512];
  std::snprintf(fileName, sizeof(fileName), fileFormat, fileNumber);
  std::string fullPath = std::string(folderPath) + fileName;

  // 拡張子が .h5/.hdf5 なら HDF5
  std::string ext;
  auto pos = fullPath.find_last_of('.');
  if (pos != std::string::npos) {
    ext = fullPath.substr(pos);
    // 小文字化しておくと堅牢
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  } else {
    ext.clear();  // ドットなし
  }

  std::unique_ptr<IParticleReader> reader;  
  std::vector<FieldSpec> format;
  
  switch (readFileFormat) {
  case FileFormat::Auto:  
    // 2) 拡張子／useHDF5 フラグでリーダーの種類を決定
#ifdef HAVE_HDF5
    if (ext == ".h5" || ext == ".hdf5") {
      reader = std::make_unique<HDF5Reader>();
      format = formatTokens_hdf5;
    }
#endif
    
    if(!reader){
#ifdef USE_MMAP
      reader = std::make_unique<MMapReader>();
#else
      reader = std::make_unique<BinaryReader>();
#endif
      format = formatTokens;
    }    
    break;
  
#ifdef HAVE_HDF5
  case FileFormat::HDF5: {
    reader = std::make_unique<HDF5Reader>();
    format = formatTokens_hdf5;
    break;
  }
#endif

  case FileFormat::Binary: {
#ifdef USE_MMAP
    reader = std::make_unique<MMapReader>();
#else
    reader = std::make_unique<BinaryReader>();
#endif
    format = formatTokens;
    break;
  }

  case FileFormat::Gadget: {
    //reader = std::make_unique<BlockwiseReader>();    
    //format = formatToken;
    break;
  }

  case FileFormat::Framed: {
    //reader = std::make_unique<FramedBinaryReader>();
    //format = formatToken;
    break;
  }
  default: {
    std::cerr<<"Unknown FileFormat\n";
    return false;
  }
  }

  // npart を reader が知るなら outBlock.resize(npart) を呼ぶ必要はなく、readAll 内で確保でもOK
  if(!reader->tryFixAndCheckBinary(fullPath, outBlock.header, format)){
    std::cerr<<"the format is incorrect\n";
    return false;
  }

  if (!reader->open(fullPath, outBlock.header)){
    std::cerr<<"failed to open the file: "<< fullPath << "\n";
    return false;
  }
  
  {
    TIME_SCOPE("parse header");

    bool ok = false;
    if (enableMask) {
      ParticleMask pmask{currentMaskConfig};
      ok = reader->readRange(outBlock, 0, reader->particleCount(),
			     format, &pmask);
    }

    if (!ok) {
      ok = reader->readAll(outBlock, format);
    }
    
    if (!ok) {
      std::cerr << "Failed to read particle data: " << fullPath << "\n";
      reader->close();
      return false;
    }
  }
  reader->close();
  	
  return true;
}


#include <nanoflann.hpp>

namespace{
  // 星粒子の構造体
  struct starParticle {
    float pos[3];
    double mass;
    int type;
    int index;
    double density; // 密度を格納するフィールド
    // 他のメンバ...
  };

  // nanoflann用のデータコンテナ
  struct StarParticleCloud {
    TrackingVector<starParticle> particles;

    // kd-tree インターフェース
    inline size_t kdtree_get_point_count() const { return particles.size(); }
    
    // 指定インデックスの次元 dim の値を返す
    inline float kdtree_get_pt(const size_t idx, const size_t dim) const {
      return particles[idx].pos[dim];
    }
    
    // バウンディングボックスは省略（falseを返す）
    template <class BBOX>
    bool kdtree_get_bbox(BBOX & /*bb*/) const { return false; }
  };

  // kd-treeの型定義（3次元用）
  typedef nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<float, StarParticleCloud>,
    StarParticleCloud,
    3 /* dim */
    > KDTreeType;
}


static inline double cubic_spline_W(double r, double h) {
  const double q = r / h;
  const double sigma = 1.0 / (M_PI * h * h * h);
  if (q < 1.0) {
    return sigma * (1.0 - 1.5 * q * q + 0.75 * q * q * q);
  } else if (q < 2.0) {
    const double term = 2.0 - q;
    return sigma * (0.25 * term * term * term);
  } else {
    return 0.0;
  }
}


  // 各星粒子について、探索半径 searchRadius 内の全粒子の質量を合計し、
  // 面積 (π * searchRadius²) で割ることで密度 (Msun/pc²) を計算する関数
void ParticleArray::computeStellarDensity(const std::array<bool,6>& selType, bool flag_overwrite_hsml)
{
  const int N_neighbours = 32;

  bool flag_star = false;
  if(selType[3] == true || selType[4] == true || selType[5] == true)
    flag_star = true;

  TrackingVector<ParticleData> & particles = particleBlock.particles;
  
  // type >= 3 の粒子のみを抽出
  TrackingVector<starParticle> filtered;
  for (size_t i=0;i<particles.size();i++)
    {
      const ParticleData& p = particles[i];

      const int t = (int)p.type;
      if (t < 0 || t >= 6) continue;
      if (!selType[t]) continue;

      struct starParticle sp;
      sp.type = p.type;
      sp.pos[0] = p.pos[0];
      sp.pos[1] = p.pos[1];
      sp.pos[2] = p.pos[2];
      sp.mass = p.mass;
      sp.index = i;
	
      filtered.push_back(sp);      
    }
  
  TrackingVector<double> densities(filtered.size(), 0.0);

  // データコンテナにコピー（必要に応じて参照やポインタを使ってもよい）
  StarParticleCloud cloud;
  cloud.particles = filtered;
    
  // kd-treeの構築
  KDTreeType kdTree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10 /* max leaf */));
  kdTree.buildIndex();

  // knnSearch の結果を格納するためのコンテナ
  TrackingVector<KDTreeType::IndexType> ret_indexes(N_neighbours);
  TrackingVector<float> out_dists_sqr(N_neighbours);

  double cosmofac = 1.;
  if(units.useComovingCoordinate)
    cosmofac = particleBlock.header.time;

  if(cosmofac < 1.e-2 || cosmofac > 1.)
    cosmofac = 1.;

  double hubble = units.hubble;
  if(hubble < 0.1 || hubble > 1.0)
    hubble = 1.;
  
  nanoflann::SearchParameters params;
  // 各粒子について近傍を探索
  for (size_t i = 0; i < cloud.particles.size(); i++) {
    const auto& pi = cloud.particles[i];
    float query_pt[3] = {
      cloud.particles[i].pos[0],
      cloud.particles[i].pos[1],
      cloud.particles[i].pos[2]
    };

    size_t num_results = kdTree.knnSearch(&query_pt[0], N_neighbours, ret_indexes.data(), out_dists_sqr.data());    
    if (num_results == 0)
      continue;

    double h = std::sqrt(out_dists_sqr[num_results - 1]);
    if (h <= 0) continue;

    double totalMass = 0., density = 0.;

    for (size_t j = 0; j < num_results; j++)
      {
	size_t idx = ret_indexes[j];
	
	const auto& pj = cloud.particles[idx];

	double dx = pi.pos[0] - pj.pos[0];
	double dy = pi.pos[1] - pj.pos[1];
	double dz = pi.pos[2] - pj.pos[2];
	double r = std::sqrt(dx*dx + dy*dy + dz*dz);
	double m = pj.mass;

	totalMass += m;
	density += m * cubic_spline_W(r, h);	    	  
      }
        
    // 面積 = π * r^2
    double area = M_PI * h * h;
    
    int original_index = cloud.particles[i].index;
    if(flag_star)
      particles[original_index].density = totalMass * units.mass_msun
	/ area / std::pow(originalMax / desiredMax * cosmofac * units.length_pc, 2.) * units.hubble;
    else
      particles[original_index].density = density * units.mass_g / std::pow(originalMax / desiredMax * cosmofac * units.length_cm, 3.) * units.hubble * units.hubble;

    if(flag_overwrite_hsml)
      particles[original_index].Hsml = h;
    
    printf("i=%d mass=%g h=%g desnity=%g %g cosmofac=%g scale_len=%g hubble=%g\n"
	   , original_index, totalMass, h, particles[original_index].density, density, cosmofac, originalMax / desiredMax * cosmofac * units.length_cm, units.hubble);
  }
}

  /// ── 補助関数 ─────────────────────────────────────────────
  /// 指定された変数名から Particle の値を取得する
float ParticleData::getValue(const std::string &var) const{
  if (var == "x")
    return pos[0];
  else if (var == "y")
    return pos[1];
  else if (var == "z")
    return pos[2];
  else if (var == "r") {
    // r は粒子位置の原点からの距離として計算（必要に応じて別の中心からの距離に変更可能）
    return glm::length(glm::vec3(pos[0], pos[1], pos[2]));
  }
  else if (var == "Density")
    return density;
  else if (var == "Temperature")
    return temperature;
  else if (var == "Hsml")
    return Hsml;
  else if (var == "Mass")
    return mass;
  else {
    std::cerr << "getValue: Unknown variable \"" << var << "\". Returning 0." << std::endl;
    return 0.0f;
  }
}


float HaloData::getHaloValue(const std::string &var) const{
  if (var == "Mass")
    return GroupMass;
  else if (var == "GasMass")
    return GroupMassType[0];
  else if (var == "StellarMass")
    return GroupMassType[3] + GroupMassType[4] + GroupMassType[5];
  else if (var == "GasMetallicity")
    return GroupMetallicity[0];
  else if (var == "StellarMetallicity")
    return GroupMetallicity[1];
  else {
    std::cerr << "getValue: Unknown variable \"" << var << "\". Returning 0." << std::endl;
    return 0.0f;
  }
}

/// ── 補助関数 ─────────────────────────────────────────────
/// 指定された変数名から Particle の値を取得する
float ClumpData::getValue(const std::string &var) const{
  if (var == "Density")
    return density;
  else if (var == "Temperature")
    return temperature;
  else if (var == "ClumpMass")
    return mass;
  else if (var == "StellarMass")
    return stellar_mass;
  else {
    std::cerr << "getValue: Unknown variable \"" << var << "\". Returning 0." << std::endl;
    return 0.0f;
  }
}
