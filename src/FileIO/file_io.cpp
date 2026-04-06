#include "interaction/camera.h"
#include "compute_radial_profile.h"
#include "compute_2D_histogram.h"
#include "FileIO/file_io.h"

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
#include <random>

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
  std::mt19937_64 rng(12345);
  std::uniform_real_distribution<double> ud(-1.0, 1.0);

  const int n_side = 50;
  const double x_min = -50.;
  const double x_max = 50.;
  const double xlen = 100.;
  double dx = xlen / static_cast<double>(n_side);

  double amp = 0.001;
  double Omega = 100.;
  
  HeaderInfo header;
  header.npart = n_side * n_side * n_side;
  header.time = 0.;
  header.boxSize = xlen;
  header.flag_comoving = 0;
  header.flag_hdf5 = 0;

  TrackingVector<ParticleData> particles;
  particles.reserve(n_side * n_side * n_side);
  
  for (int i = 0; i < n_side; i++) {
    double x = x_min + dx * i;    

    for (int j = 0; j < n_side; j++) {
      double y = x_min + dx * j;
      
      for (int k = 0; k < n_side; k++) {
	double z = x_min + dx * k;

	double rx = ud(rng);
	double ry = ud(rng);
	double rz = ud(rng);

	double x_out = x + amp * rx;
	double y_out = y + amp * ry;
	double z_out = z + amp * rz;
	
	double r2 = x_out * x_out + y_out * y_out + z_out * z_out;

	ParticleData p;
	p.pos[0] = x_out;  p.pos[1] = y_out;  p.pos[2] = z_out;
	p.original_pos[0] = x_out;  p.original_pos[1] = y_out;  p.original_pos[2] = z_out;

	p.vel[0] = x_out;
	p.vel[1] = y_out;
	p.vel[2] = z_out;

	double v_rot_x = -Omega * y_out;
        double v_rot_y =  Omega * x_out;
        double v_rot_z =  0.0;
	
	p.vel[0] += v_rot_x;
	p.vel[1] += v_rot_y;
	p.vel[2] += v_rot_z;
	
	p.Hsml = dx;
	p.originalHsml = dx;

	p.mass = 1.;
	p.density = 1.;
	p.temperature = 1.;
	p.type = 0;

	p.ID = particles.size();
	
	particles.push_back(p);
      }
    }
  }

  batchParticleBlocks.resize(1);
  batchParticleBlocks[0].particles = std::move(particles);
  batchParticleBlocks[0].header = header;
  
  P->swap_particles(batchParticleBlocks, 0, 1);  
}

void FileInfo::loadNewSnapshot(int newFileIndex, ParticleArray *P){
  TIME_FUNCTION();

  if (newFileIndex >= currentBatchStart &&
      newFileIndex < currentBatchStart + batchSize * skipStep) {
    int batchIndex = (newFileIndex - currentBatchStart) / skipStep;
    if (batchIndex >= 0 && batchIndex < batchSize) 
      P->swap_particles(batchParticleBlocks, batchIndex, 0);
    
  } else {
    if (!isLoading)
      loadBatch(newFileIndex, batchSize, skipStep, P);	
  }

  currentFileIndex = newFileIndex;
  printf("currentStep=%d newFileIndex=%d currentBatchStart=%d\n", currentStep, newFileIndex, currentBatchStart);

#ifdef CLUMP_DATA_READ
  P->flag_renew_clumpList = true;
  if(P->flag_follow_clump_center){    
    ClumpData targetClump = P->Clumps[P->TargetClumpID];
    int flag = P->readClumpData(currentFileIndex);

    if(flag){
      P->flag_follow_clump_center = false;
      return;
    }
    
    float target_pos_new[3];
    targetClump.get_next_clump_position(P->Clumps, target_pos_new);
        
    float dist = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
    glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
    camCtx.cameraTarget = glm::vec3(target_pos_new[0], target_pos_new[1], target_pos_new[2]);
    camCtx.cameraPos = camCtx.cameraTarget - direction * dist;
  }
#endif
  
  if(P->flag_follow_particle_ID){
    float target_pos_new[3];
    bool flag = P->findParticleID(P->TargetParticleID, target_pos_new);

    if(flag == false){
      P->flag_follow_particle_ID = false;
      return;
    }
    
    float dist = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
    glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
    camCtx.cameraTarget = glm::vec3(target_pos_new[0], target_pos_new[1], target_pos_new[2]);
    camCtx.cameraPos = camCtx.cameraTarget - direction * dist;
  }
}


void FileInfo::syncLoadFirstFile(int targetFile, ParticleArray *P) {
  ParticleBlock pBlock;  
  if (loadSingleFile(targetFile, pBlock)) {
    {
      std::lock_guard<std::mutex> lock(g_dataMutex);
      batchParticleBlocks.resize(1);
      batchParticleBlocks[0] = std::move(pBlock);
    }

    P->swap_particles(batchParticleBlocks, 0, 1);
  } else {
    P->particleBlock.particles = TrackingVector<ParticleData>{};   
    std::cerr << "Failed to load first file: " << targetFile << std::endl;
  }
}

// 残りのファイルを非同期に読み込む関数（インデックス1以降）
void FileInfo::asyncLoadRemainingFiles(int targetFile, int batchSize, int skipStep) {
  // バッチのサイズが1以上であることが前提
  // ここでは1つ目以外のファイルを読み込みます
  TrackingVector<ParticleBlock> newBatch(batchSize - 1);
  for (int i = 1; i < batchSize; i++) {
    int fileNumber = targetFile + i * skipStep;
    ParticleBlock pBlock;
    if (loadSingleFile(fileNumber, pBlock)) {
      newBatch[i - 1] = std::move(pBlock);
    } else {
      newBatch[i - 1] = {};
    }
  }
  {
    std::lock_guard<std::mutex> lock(g_dataMutex);
    // バッチの先頭はすでに設定済みなので、残りを後ろに追加するか、必要に応じてスワップする
    for (size_t i = 0; i < newBatch.size(); i++) {
      // ここでは単純に後ろに追加する例
      batchParticleBlocks.push_back(std::move(newBatch[i]));
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
    token.label = "position"; token.type = DataType::Float; token.count = 3;
    formatTokens.push_back(token);
    token.label = "velocity"; token.type = DataType::Float; token.count = 3;
    formatTokens.push_back(token);
    token.label = "type"; token.type = DataType::Int32; token.count = 1;
    formatTokens.push_back(token);
    token.label = "ID"; token.type = DataType::Int32; token.count = 1;
    formatTokens.push_back(token);
    token.label = "Hsml"; token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.label = "density"; token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.label = "temperature"; token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.label = "dummy"; token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.label = "value"; token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.label = "value2"; token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    token.label = "dummy"; token.type = DataType::Float; token.count = 4;
    formatTokens.push_back(token);
    token.label = "mass"; token.type = DataType::Float; token.count = 1;
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

  const char* availableLabels[] = {"position", "velocity", "Bfield", "Hsml", "mass", "density", "temperature",  "value", "value2", "Metallicity", "type", "ID", "dummy",
				   "ElectronAbundance", "H2Abundance", "HDAbundance", "J21"};
    
  for (size_t i = 0; i < formatTokensEdit.size(); i++) {
    ImGui::PushID(i);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s, %s, count=%d",
		  formatTokensEdit[i].label.c_str(),
		  (formatTokensEdit[i].type == DataType::Float) ? "float" : (formatTokensEdit[i].type == DataType::Int32) ? "int": "double",
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
	ImGui::SetDragDropPayload("DND_FORMAT_TOKEN", &i, sizeof(int));
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
	
    // ----- ラベルの選択（InputText の代わりにドロップダウンを利用） -----
    // 現在のラベルが候補リストのどれに対応するか調べる
    int currentLabelIndex = 0;
    for (int j = 0; j < IM_ARRAYSIZE(availableLabels); ++j) {
      if (formatTokensEdit[i].label ==  availableLabels[j]) {
	currentLabelIndex = j;
	break;
      }
    }
    // ドロップダウン（コンボボックス）でラベルを選択
    if (ImGui::Combo("Label", &currentLabelIndex, availableLabels, IM_ARRAYSIZE(availableLabels))) {
      // 選択が変更された場合、トークンのラベルを更新
      formatTokensEdit[i].label = availableLabels[currentLabelIndex];
      // ここで、選択されたラベルに応じて型を自動的に設定する例
      if (strcmp(availableLabels[currentLabelIndex], "type") == 0) {
	// 例: "type" が選ばれたら整数型にする
	formatTokensEdit[i].type = DataType::Int32;
	formatTokensEdit[i].count = 1;
      }
	    
      if (strcmp(availableLabels[currentLabelIndex], "ID") == 0) {
	// 例: "type" が選ばれたら整数型にする
	formatTokensEdit[i].type = DataType::Int32;;
	formatTokensEdit[i].count = 1;
      }

      if (strcmp(availableLabels[currentLabelIndex], "density") == 0) {
	// 例: "value" が選ばれたら浮動小数点型にする
	formatTokensEdit[i].type = DataType::Float;
	formatTokensEdit[i].count = 1;
      }

      if (strcmp(availableLabels[currentLabelIndex], "temperature") == 0) {
	// 例: "value" が選ばれたら浮動小数点型にする
	formatTokensEdit[i].type = DataType::Float;
	formatTokensEdit[i].count = 1;
      }

      if (strcmp(availableLabels[currentLabelIndex], "Bfield") == 0) {
	// 例: "value" が選ばれたら浮動小数点型にする
	formatTokensEdit[i].type = DataType::Float;
	formatTokensEdit[i].count = 3;
      }

      if (strcmp(availableLabels[currentLabelIndex], "Metallicity") == 0) {
	// 例: "value" が選ばれたら浮動小数点型にする
	formatTokensEdit[i].type = DataType::Float;
	formatTokensEdit[i].count = 1;
      }
	    
      if (strcmp(availableLabels[currentLabelIndex], "value") == 0) {
	// 例: "value" が選ばれたら浮動小数点型にする
	formatTokensEdit[i].type = DataType::Float;
	formatTokensEdit[i].count = 1;
      }

      if (strcmp(availableLabels[currentLabelIndex], "value2") == 0) {
	// 例: "value" が選ばれたら浮動小数点型にする
	formatTokensEdit[i].type = DataType::Float;
	formatTokensEdit[i].count = 1;
      }

      if (strcmp(availableLabels[currentLabelIndex], "position") == 0) {
	// 例: "value" が選ばれたら浮動小数点型にする
	formatTokensEdit[i].type = DataType::Float;
	formatTokensEdit[i].count = 3;
      }

      if (strcmp(availableLabels[currentLabelIndex], "velocity") == 0) {
	// 例: "value" が選ばれたら浮動小数点型にする
	formatTokensEdit[i].type = DataType::Float;
	formatTokensEdit[i].count = 3;
      }

      if (strcmp(availableLabels[currentLabelIndex], "mass") == 0) {
	// 例: "value" が選ばれたら浮動小数点型にする
	formatTokensEdit[i].type = DataType::Float;
	formatTokensEdit[i].count = 1;
      }

      if (strcmp(availableLabels[currentLabelIndex], "dummy") == 0) {
	// 例: "value" が選ばれたら浮動小数点型にする
	formatTokensEdit[i].type = DataType::Float;
	formatTokensEdit[i].count = 1;
      }
    }
	
    const char* types[] = { "float", "int", "int64", "double" };
    int currentType = static_cast<int>(formatTokensEdit[i].type);
    ImGui::Combo("Type", &currentType, types, IM_ARRAYSIZE(types));
    formatTokensEdit[i].type = static_cast<DataType>(currentType);	
    ImGui::InputInt("Count", &formatTokensEdit[i].count);
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
    newToken.label = "dummy";
    newToken.type = DataType::Float;
    newToken.count = 1;
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

  for (auto &tok : formatTokensEdit) 
    if (tok.displayName.empty()) 
      FieldSpec::SetDefaultDisplayName(tok);
  
  const char* availableLabels[] = {"position", "velocity", "Bfield", "Hsml", "Volume", "mass", "density", "temperature",  "value", "value2", "ID", "internalenergy",
				   "ElectronAbundance", "H2Abundance", "HDAbundance", "J21", "Gamma", "Metallicity"};

  // テーブルを使って列をそろえる
  if (ImGui::BeginTable("FieldTable", 6,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
    ImGui::TableSetupColumn("##", ImGuiTableColumnFlags_WidthFixed, 20.0f);
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
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
      if (ImGui::BeginCombo("##labelCombo", formatTokensEdit[i].label.c_str())) {
	for (int n = 0; n < IM_ARRAYSIZE(availableLabels); n++) {
	  bool is_selected = (formatTokensEdit[i].label == availableLabels[n]);
	  if (ImGui::Selectable(availableLabels[n], is_selected)) {
	    formatTokensEdit[i].label = availableLabels[n];
	    if (strcmp(availableLabels[n], "ID") == 0)
	      formatTokensEdit[i].type = DataType::Int32;
	    else
	      formatTokensEdit[i].type = DataType::Float;
	    
	    FieldSpec::SetDefaultDisplayName(formatTokensEdit[i]);
	  }
	  if (is_selected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
      }

      // DisplayName (手入力)
      ImGui::TableNextColumn();
      ImGui::PushItemWidth(-1);
      char buf[128];
      std::snprintf(buf, sizeof(buf), "%s", formatTokensEdit[i].displayName.c_str());
      
      if (ImGui::InputText("##displayName", buf, IM_ARRAYSIZE(buf))) {
	formatTokensEdit[i].displayName = buf;
      }
      
      ImGui::PopItemWidth();

      // Type 列
      const char* types[] = { "float", "int", "int64", "double" };
      ImGui::TableNextColumn();
      int currentType = static_cast<int>(formatTokensEdit[i].type);
      if (ImGui::Combo("##type", &currentType, types, IM_ARRAYSIZE(types))) {
	formatTokensEdit[i].type = static_cast<DataType>(currentType);
      }

      // Count 列
      ImGui::TableNextColumn();
      ImGui::InputInt("##count", &formatTokensEdit[i].count, 1, 10);
      if (formatTokensEdit[i].count < 1) formatTokensEdit[i].count = 1;

      // Delete ボタン
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
    newToken.label = "dummy";
    newToken.type  = DataType::Float;
    newToken.count = 1;
    newToken.displayName.clear();
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

  outBlock.header.UnitLength_in_cm = UnitLength_in_cm;
  outBlock.header.UnitMass_in_g = UnitMass_in_g;
  outBlock.header.UnitVelocity_in_cm_per_s = UnitVelocity_in_cm_per_s;
  outBlock.header.HubbleParam = Hubble;
  
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
  
  IOPlan plan = buildPlanFromToks(format);
  {
    TIME_SCOPE("parse header");

    bool ok = false;
    if (enableMask) {
      ParticleMask pmask{currentMaskConfig};
      ok = reader->readRangeMasked(outBlock, 0, reader->particleCount(),
				   format, plan, pmask);
    }

    if (!ok) {
      ok = reader->readAll(outBlock, format, plan);
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


void ParticleArray::swap_particles(TrackingVector<ParticleBlock>& batchP, int ibatch, int flag_reset) {
  TIME_FUNCTION();

  float newMin[kMaxQ][kNumTypes];
  float newMax[kMaxQ][kNumTypes];

  float maxVal = 0.0f;

  ParticleBlock& newParticleBlock = batchP[ibatch];
  TrackingVector<ParticleData>& newParticles = newParticleBlock.particles;

  // 1) 今回の block に応じて「有効 quantity リスト」を確定
  newParticleBlock.rebuildQuantities();

  if (!newParticles.empty()) {
    // 2) 初期化（有効分だけでOK）
    for (int q = 0; q < newParticleBlock.nUIQ; ++q) {
      for (int t = 0; t < kNumTypes; ++t) {
	newMin[q][t] = std::numeric_limits<float>::max();
	newMax[q][t] = -std::numeric_limits<float>::max();
      }
    }

    int npart_type[kNumTypes] = {0,0,0,0,0,0};

#pragma omp parallel
    {
      float localMax = 0.0f;
      float localMin[kMaxQ][kNumTypes], localMaxV[kMaxQ][kNumTypes];
      int   local_npart_type[kNumTypes] = {0};

      // thread-local init（有効分だけ）
      for (int q = 0; q < newParticleBlock.nUIQ; ++q) {
        for (int t = 0; t < kNumTypes; ++t) {
          localMin[q][t]  = std::numeric_limits<float>::max();
          localMaxV[q][t] = -std::numeric_limits<float>::max();
        }
      }

#pragma omp for
      for (int i = 0; i < (int)newParticles.size(); ++i) {
        ParticleData& p = newParticles[i];
        int type = p.type;
        local_npart_type[type]++;

        // maxVal 用
        float m = std::max(std::fabs(p.original_pos[0]),
			   std::max(std::fabs(p.original_pos[1]), std::fabs(p.original_pos[2])));
        if (m > localMax) localMax = m;

        // pos/Hsml の初期化（あなたの元コード踏襲）
        for (int k = 0; k < 3; ++k) p.pos[k] = p.original_pos[k];
        p.Hsml = p.originalHsml;
        p.flag_stress = 0;

        // min/max 更新（有効 quantity 分だけ）
        for (int q = 0; q < newParticleBlock.nUIQ; ++q) {
          float v = getScalarValue(newParticleBlock, p, i, newParticleBlock.uiQ[q]);
          localMin[q][type]  = std::min(localMin[q][type],  v);
          localMaxV[q][type] = std::max(localMaxV[q][type], v);
        }
      }

#pragma omp critical
      {
        if (localMax > maxVal) maxVal = localMax;

        for (int t = 0; t < kNumTypes; ++t) npart_type[t] += local_npart_type[t];

        for (int q = 0; q < newParticleBlock.nUIQ; ++q)
          for (int t = 0; t < kNumTypes; ++t) {
            newMin[q][t] = std::min(newMin[q][t], localMin[q][t]);
            newMax[q][t] = std::max(newMax[q][t], localMaxV[q][t]);
          }
      }
    } // omp parallel

    // 3) スケーリング（maxVal 確定後）
    if (maxVal > 0.0f) {
      float invMaxVal = desiredMax / maxVal;
#pragma omp parallel for
      for (int i = 0; i < (int)newParticles.size(); ++i) {
        ParticleData& p = newParticles[i];
        for (int k = 0; k < 3; ++k) p.pos[k] *= invMaxVal;
        p.Hsml *= invMaxVal;
      }
    }

    // 4) type に粒子がいない場合の min/max を 0 に（あなたの元コード踏襲）
    for (int q = 0; q < newParticleBlock.nUIQ; ++q) {
      for (int t = 0; t < kNumTypes; ++t) {
        if (npart_type[t] == 0) {
          newMin[q][t] = 0.0f;
          newMax[q][t] = 0.0f;
        }
      }
    }
  } // newParticles not empty

  // 5) 古いブロックを戻す（元コード踏襲）
  if (flag_reset == 0)
    batchP[particleBlock_index] = std::move(particleBlock);

  // 6) 新しいブロックを採用
  particleBlock = std::move(newParticleBlock);
  particleBlock_index = ibatch;

  // 7) particleValueMin/Max に格納（有効分だけ上書き）
  for (int q = 0; q < particleBlock.nUIQ; ++q)
    for (int t = 0; t < kNumTypes; ++t) {
      particleValueMin[q][t] = newMin[q][t];
      particleValueMax[q][t] = newMax[q][t];
    }

  originalMax = maxVal;

  // 8) 以降あなたのユニット・フラグ更新はそのまま
  if (particleBlock.header.flag_hdf5 == true) {
    UnitLength_in_cm   = particleBlock.header.UnitLength_in_cm;
    UnitMass_in_g      = particleBlock.header.UnitMass_in_g;
    UnitVelocity_in_cm_per_s = particleBlock.header.UnitVelocity_in_cm_per_s;
    Hubble             = particleBlock.header.HubbleParam;
    useComovingCorrdinate = particleBlock.header.flag_comoving;
    setUnits();
  }

  flag_mask.resize(particleBlock.particles.size(), 0);

  particlesDirty = true;
  flagParticleIndexDirty = true;
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
  if(useComovingCorrdinate)
    cosmofac = particleBlock.header.time;

  if(cosmofac < 1.e-2 || cosmofac > 1.)
    cosmofac = 1.;

  double hubble = Hubble;
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
      particles[original_index].density = totalMass * UnitMass_in_msolar
	/ area / std::pow(originalMax / desiredMax * cosmofac * UnitLength_in_pc, 2.) * hubble;
    else
      particles[original_index].density = density * UnitMass_in_msolar * 1.998e33 / std::pow(originalMax / desiredMax * cosmofac * UnitLength_in_cm, 3.) * hubble * hubble;

    if(flag_overwrite_hsml)
      particles[original_index].Hsml = h;
    
    printf("i=%d mass=%g h=%g desnity=%g %g cosmofac=%g scale_len=%g hubble=%g %g\n"
	   , original_index, totalMass, h, particles[original_index].density, density, cosmofac, originalMax / desiredMax * cosmofac * UnitLength_in_cm, hubble, Hubble);
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
