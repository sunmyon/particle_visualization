#include "main.h"
#include "camera.h"
#include "compute_radial_profile.h"
#include "compute_2D_histogram.h"
#include "FileIO/file_io.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "implot.h"

#include <chrono>
#include <iomanip>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <future>
#include <thread>
#include <fstream>
#include <cstring>
#include <random>

TrackingVector<int> FileInfo::getStarParticleID(int indexFile){
  TrackingVector<ParticleData> particles;
  HeaderInfo header;
  loadSingleFile(indexFile, particles, header);

  TrackingVector<int> IDs;
  
  for(auto &p : particles){
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

	double dx = ud(rng);
	double dy = ud(rng);
	double dz = ud(rng);

	double x_out = x + amp * dx;
	double y_out = y + amp * dy;
	double z_out = z + amp * dz;
	
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
	
	particles.push_back(p);
      }
    }
  }

  batchParticles.resize(1);
  batchParticles[0] = std::move(particles);
  headerBatch.resize(1);
  headerBatch[0] = header;
  
  P->swap_particles(batchParticles, 0, headerBatch[0], 1);  
}

void FileInfo::loadNewSnapshot(int newFileIndex, ParticleArray *P){
  if (newFileIndex >= currentBatchStart &&
      newFileIndex < currentBatchStart + batchSize * skipStep) {
    int batchIndex = (newFileIndex - currentBatchStart) / skipStep;
    if (batchIndex >= 0 && batchIndex < batchSize) 
      P->swap_particles(batchParticles, batchIndex, headerBatch[batchIndex], 0);
    
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
  TrackingVector<ParticleData> firstParticles;
  HeaderInfo firstHeader;
  if (loadSingleFile(targetFile, firstParticles, firstHeader)) {
    {
      std::lock_guard<std::mutex> lock(g_dataMutex);
      // 例えば、バッチの最初の要素として設定
      batchParticles.resize(1);
      batchParticles[0] = std::move(firstParticles);
      headerBatch.resize(1);
      headerBatch[0] = firstHeader;
    }
    // 最初のファイルのデータを即座に反映する

    P->swap_particles(batchParticles, 0, headerBatch[0], 1);
  } else {
    // 読み込みに失敗した場合の処理
    P->particles = TrackingVector<ParticleData>{};   
    std::cerr << "Failed to load first file: " << targetFile << std::endl;
  }
}

// 残りのファイルを非同期に読み込む関数（インデックス1以降）
void FileInfo::asyncLoadRemainingFiles(int targetFile, int batchSize, int skipStep) {
  // バッチのサイズが1以上であることが前提
  // ここでは1つ目以外のファイルを読み込みます
  TrackingVector<TrackingVector<ParticleData>> newBatch(batchSize - 1);
  TrackingVector<HeaderInfo> newheaderBatch(batchSize - 1);
  for (int i = 1; i < batchSize; i++) {
    int fileNumber = targetFile + i * skipStep;
    TrackingVector<ParticleData> particles;
    HeaderInfo header;
    if (loadSingleFile(fileNumber, particles, header)) {
      newBatch[i - 1] = std::move(particles);
      newheaderBatch[i - 1] = header;
    } else {
      newBatch[i - 1] = TrackingVector<ParticleData>(); // 読み込み失敗時は空
      newheaderBatch[i - 1] = {};
    }
  }
  {
    std::lock_guard<std::mutex> lock(g_dataMutex);
    // バッチの先頭はすでに設定済みなので、残りを後ろに追加するか、必要に応じてスワップする
    for (size_t i = 0; i < newBatch.size(); i++) {
      // ここでは単純に後ろに追加する例
      batchParticles.push_back(std::move(newBatch[i]));
      headerBatch.push_back(newheaderBatch[i]);
    }
  }
  // 必要なら、メインスレッドで後から swap_particles するタイミングを設ける
}


void FileInfo::loadBatch(int targetFile, int batchSize, int skipStep, ParticleArray *P) {
  isLoading = true;
  // 最初のファイルを同期的に読み込む
  syncLoadFirstFile(targetFile, P);
  currentBatchStart = targetFile;  // 新たなバッチの開始位置
    
  // 残りのファイルは非同期で読み込む
  std::future<void> asyncLoader = std::async(std::launch::async,
					     &FileInfo::asyncLoadRemainingFiles, this,
					     targetFile, batchSize, skipStep);
  // 必要に応じてここで asyncLoader.get() を呼んで同期させることも可能です
  // ただし、初回だけ最初のファイルを同期させたいのであれば、ここは非同期のままでもよいでしょう

  isLoading = false;
}


void ParticleArray::ShowHaloesUI() {
  if (!showWindowHaloes) return;

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);  
  ImGui::Begin("Halo lists", &showWindowHaloes, ImGuiWindowFlags_None);
  
  // 粒子の種類と表示件数 m をユーザーが入力できるようにする
  static int m = 10;             // デフォルトは上位 10 件

  // ユーザーが入力できるウィジェット
  ImGui::InputInt("Number of Halo list (out of %zu halos)", &m, Haloes.size());

  // 入力値の検証（例：粒子タイプは 0～5 の間）
  if (m < 1) m = 1;

  // 表示件数は m 件（件数が足りなければ全件表示）
  int count = std::min(m, static_cast<int>(Haloes.size()));

  ImGui::Text("Showing top %d haloes", count);
  for (int i = 0; i < count; i++) {
    char label[200];
    std::snprintf(label, sizeof(label),
		  "ID %d: mass = %.3g (gas=%g stars=%g), pos = (%.2g, %.2g, %.2g), metallicity=%g %g"
		  , i
		  , Haloes[i].GroupMass
		  , Haloes[i].GroupMassType[0]
		  , Haloes[i].GroupMassType[3] + Haloes[i].GroupMassType[4] + Haloes[i].GroupMassType[5]
		  , Haloes[i].GroupPos[0], Haloes[i].GroupPos[1], Haloes[i].GroupPos[2]
		  , Haloes[i].GroupMetallicity[0], Haloes[i].GroupMetallicity[1]);
    
    if (ImGui::Selectable(label)) {
      // 選択された粒子の位置をカメラの注視点に設定
      float distance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
      glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);

      float pos[3];
      pos[0] = Haloes[i].GroupPos[0] * desiredMax / originalMax;
      pos[1] = Haloes[i].GroupPos[1] * desiredMax / originalMax;
      pos[2] = Haloes[i].GroupPos[2] * desiredMax / originalMax;
      
      camCtx.cameraTarget = glm::vec3(pos[0], pos[1], pos[2]);
      camCtx.cameraPos = camCtx.cameraTarget - direction * distance;
    }
  }

  ImGui::Text("Plot halo histogram");
  
  const char* quantities[] = { "Mass", "GasMass", "StellarMass", "GasMetallicity", "StellarMetallicity"};
  // 各軸に使う変数のインデックス（デフォルトでは X 軸に "x"、Y 軸に "y" を選択）
  static int selectedVar = 0;
  ImGui::Combo("Quantity", &selectedVar, quantities, IM_ARRAYSIZE(quantities));
  std::string var = quantities[selectedVar];
  
  // ビン数の入力
  static int bins = 20;
  ImGui::InputInt("Number of bins", &bins);

  static bool histogramLogScaleX = true;
  static bool histogramLogScaleY = true;  
  ImGui::Checkbox("Use Log scale X", &histogramLogScaleX);
  ImGui::Checkbox("Use Log scale Y", &histogramLogScaleY);

  // 自動レンジを使うかどうかのチェックボックス
  static bool autoRange = true;
  ImGui::Checkbox("Auto Range", &autoRange);
  
  // 手動レンジ入力用（autoRange==false の場合）
  static float range1_min = 0.0f, range1_max = 1.0f;
  static float range2_min = 0.0f, range2_max = 1.0f;
  if (!autoRange)
    {
      ImGui::InputFloat("X Axis Min", &range1_min, 0.0f, 0.0f, "%g");
      ImGui::InputFloat("X Axis Max", &range1_max, 0.0f, 0.0f, "%g");
      ImGui::InputFloat("Y Axis Min", &range2_min, 0.0f, 0.0f, "%g");
      ImGui::InputFloat("Y Axis Max", &range2_max, 0.0f, 0.0f, "%g");
    }

  static bool histogramComputed = false;
  static TrackingVector<float> histBins(bins);
  static TrackingVector<float> binCenters(bins);
  static float vmin, vmax, binSize;
  
  // ④ ヒストグラム作成（対象全粒子、ここでは質量を例とする）
  if (ImGui::Button("Compute 1D Histogram")) {
    
    float massMin = std::numeric_limits<float>::max();
    float massMax = std::numeric_limits<float>::lowest();
    for (const auto &p : Haloes) {
      float mass = p.getHaloValue(var);      
      if(histogramLogScaleX)
	mass = log10(mass);
      
      massMin = std::min(massMin, mass);
      massMax = std::max(massMax, mass);
    }
    
    if (massMin == massMax)
      massMax = massMin + 1.0f;

    if (autoRange){
      range1_min = massMin;
      range1_max = massMax;
    }
    
    // ヒストグラムの各ビンのカウント
    TrackingVector<int> binCounts(bins, 0);
    binSize = (range1_max - range1_min) / bins;
    for (const auto &p : Haloes) {
      float mass = p.getHaloValue(var);      
      if(histogramLogScaleX)
	mass = log10(mass);
      
      int bin = static_cast<int>((mass - range1_min) / binSize);

      printf("mass=%g min=%g bin=%d\n", mass, range1_min, bin);
      
      if (bin < 0) bin = 0;
      if (bin >= bins) bin = bins - 1;
      binCounts[bin]++;
    }

    vmin = std::numeric_limits<float>::max();
    vmax = std::numeric_limits<float>::lowest();    

    // ImPlot用にfloat配列に変換
    for (int i = 0; i < bins; i++) {      
      histBins[i] = static_cast<float>(binCounts[i]);
      
      float value = histBins[i];
      if(histogramLogScaleY){
	if(value == 0.)
	  continue;
	
	value = log10(value);
      }
      
      vmin = std::min(vmin, value);
      vmax = std::max(vmax, value);	      
    }

    if(histogramLogScaleY){
      vmin = std::floor(vmin);
      vmax = std::ceil(vmax);

      vmin = 0.8*std::pow(10., vmin);
      vmax = std::pow(10., vmax);
    }else{
      vmin = 0.;

      int digits = static_cast<int>(log10(vmax));
      double scale = std::pow(10., digits);
      vmax = std::ceil(vmax / scale) * scale;
    }

    if (autoRange){
      range2_min = vmin;
      range2_max = vmax;
    }
    
    // X軸（ビン中心）の値
    for (int i = 0; i < bins; i++) 
      binCenters[i] = range1_min + (i + 0.5f) * binSize;    

    histogramComputed = true;
  }

  if(histogramComputed){
    // ヒストグラム描画（ImPlotを利用）
    if (ImPlot::BeginPlot("Mass Histogram", ImVec2(-1,300))) {
      // PlotHistogram expects an array of counts and optionally bin centers;
      // ここではカウントのみプロットします。

      if (histogramLogScaleY)
	ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
      else
	ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
      
      ImPlot::SetupAxisLimits(ImAxis_X1, range1_min, range1_max, ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, range2_min, range2_max, ImGuiCond_Always);
            
      //ImPlot::PlotHistogram("Mass", histBins.data(), bins, bins, 1., ImPlotRange(range1_min, range1_max));
      ImPlot::PlotBars("Mass", binCenters.data(), histBins.data(), bins, binSize);
      ImPlot::EndPlot();
    }
  }

    
  ImGui::End();
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
    FormatToken token;
    std::strcpy(token.label, "position"); token.type = DataType::Float; token.count = 3;
    formatTokens.push_back(token);
    std::strcpy(token.label, "dummy"); token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    std::strcpy(token.label, "value"); token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    std::strcpy(token.label, "value2"); token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    std::strcpy(token.label, "mass"); token.type = DataType::Float; token.count = 1;
    formatTokens.push_back(token);
    std::strcpy(token.label, "dummy"); token.type = DataType::Float; token.count = 10;
    formatTokens.push_back(token);
    std::strcpy(token.label, "Hsml"); token.type = DataType::Float; token.count = 10;
    formatTokens.push_back(token);
    std::strcpy(token.label, "density"); token.type = DataType::Float; token.count = 10;
    formatTokens.push_back(token);
    std::strcpy(token.label, "temperature"); token.type = DataType::Float; token.count = 10;
    formatTokens.push_back(token);
    std::strcpy(token.label, "type"); token.type = DataType::Int32; token.count = 1;
    formatTokens.push_back(token);
    std::strcpy(token.label, "ID"); token.type = DataType::Int32; token.count = 1;
    formatTokens.push_back(token);
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

    const char* availableLabels[] = {"position", "velocity", "Hsml", "mass", "density", "temperature",  "value", "value2", "type", "ID", "dummy"};
    
    for (size_t i = 0; i < formatTokensEdit.size(); i++) {
        ImGui::PushID(i);

	char buf[128];
	std::snprintf(buf, sizeof(buf), "%s, %s, count=%d",
                      formatTokensEdit[i].label,
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
                    FormatToken token = formatTokensEdit[srcIndex];
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
            if (strcmp(formatTokensEdit[i].label, availableLabels[j]) == 0) {
                currentLabelIndex = j;
                break;
            }
        }
        // ドロップダウン（コンボボックス）でラベルを選択
        if (ImGui::Combo("Label", &currentLabelIndex, availableLabels, IM_ARRAYSIZE(availableLabels))) {
            // 選択が変更された場合、トークンのラベルを更新
            std::strcpy(formatTokensEdit[i].label, availableLabels[currentLabelIndex]);
	    // ここで、選択されたラベルに応じて型を自動的に設定する例
            if (strcmp(availableLabels[currentLabelIndex], "type") == 0) {
                // 例: "type" が選ばれたら整数型にする
                formatTokensEdit[i].type = DataType::Int32;
            }
	    
            if (strcmp(availableLabels[currentLabelIndex], "ID") == 0) {
                // 例: "type" が選ばれたら整数型にする
                formatTokensEdit[i].type = DataType::Int32;;
            }

            if (strcmp(availableLabels[currentLabelIndex], "density") == 0) {
                // 例: "value" が選ばれたら浮動小数点型にする
                formatTokensEdit[i].type = DataType::Float;
            }

	    if (strcmp(availableLabels[currentLabelIndex], "temperature") == 0) {
                // 例: "value" が選ばれたら浮動小数点型にする
                formatTokensEdit[i].type = DataType::Float;
            }
	    
            if (strcmp(availableLabels[currentLabelIndex], "value") == 0) {
                // 例: "value" が選ばれたら浮動小数点型にする
                formatTokensEdit[i].type = DataType::Float;
            }

	    if (strcmp(availableLabels[currentLabelIndex], "value2") == 0) {
                // 例: "value" が選ばれたら浮動小数点型にする
                formatTokensEdit[i].type = DataType::Float;
            }

	    if (strcmp(availableLabels[currentLabelIndex], "position") == 0) {
                // 例: "value" が選ばれたら浮動小数点型にする
                formatTokensEdit[i].type = DataType::Float;
            }

	    if (strcmp(availableLabels[currentLabelIndex], "velocity") == 0) {
                // 例: "value" が選ばれたら浮動小数点型にする
                formatTokensEdit[i].type = DataType::Float;
            }

	    if (strcmp(availableLabels[currentLabelIndex], "mass") == 0) {
                // 例: "value" が選ばれたら浮動小数点型にする
                formatTokensEdit[i].type = DataType::Float;
            }

	    if (strcmp(availableLabels[currentLabelIndex], "dummy") == 0) {
                // 例: "value" が選ばれたら浮動小数点型にする
                formatTokensEdit[i].type = DataType::Float;
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
        FormatToken newToken;
        std::strcpy(newToken.label, "dummy");
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

  for (auto &tok : formatTokensEdit) {
    if (tok.displayName[0] == '\0') {
      FormatToken::SetDefaultDisplayName(tok);
    }
  }
  
  const char* availableLabels[] = {"position", "velocity", "Hsml", "mass", "density", "temperature",  "value", "value2", "ID", "internalenergy", "electronfraction", "H2fraction", "Gamma"};

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
      if (ImGui::BeginCombo("##labelCombo", formatTokensEdit[i].label)) {
	for (int n = 0; n < IM_ARRAYSIZE(availableLabels); n++) {
	  bool is_selected = (strcmp(formatTokensEdit[i].label, availableLabels[n]) == 0);
	  if (ImGui::Selectable(availableLabels[n], is_selected)) {
	    strcpy(formatTokensEdit[i].label, availableLabels[n]);
	    if (strcmp(availableLabels[n], "ID") == 0)
	      formatTokensEdit[i].type = DataType::Int32;
	    else
	      formatTokensEdit[i].type = DataType::Float;
	    
	    FormatToken::SetDefaultDisplayName(formatTokensEdit[i]);
	  }
	  if (is_selected) ImGui::SetItemDefaultFocus();
	}
	ImGui::EndCombo();
      }

      // DisplayName (手入力)
      ImGui::TableNextColumn();
      ImGui::PushItemWidth(-1);
      ImGui::InputText("##displayName", formatTokensEdit[i].displayName,
		       IM_ARRAYSIZE(formatTokensEdit[i].displayName));
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
    FormatToken newToken;
    strcpy(newToken.label, "dummy");
    newToken.type  = DataType::Float;
    newToken.count = 1;
    std::strncpy(newToken.displayName, "", sizeof(newToken.displayName));
    newToken.displayName[sizeof(newToken.displayName)-1] = '\0';    
    formatTokensEdit.push_back(newToken);
  }
  ImGui::SameLine();
  // 確定・キャンセル
  if (ImGui::Button("OK")) {
    formatTokens = formatTokensEdit;
    showHDF5MappingDialog = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    showHDF5MappingDialog = false;
  }
    
  ImGui::End();
}

#endif

// ------------------------------
// computeFormatInfo : 各トークンからレコードサイズと必要フィールドのオフセットを計算
// ------------------------------
bool FileInfo::computeFormatInfo(const TrackingVector<FormatToken>& tokens, FormatInfo& info)
{
  info.recordSize = 0;
  info.tokens = tokens;
  
  for (const auto &token : tokens) {
    std::string lbl(token.label);
    size_t size_item = dataTypeSize(token.type);
    
    if (lbl == "position") {
      info.posOffset = info.recordSize;
      if (token.count != 3) {
	std::cerr << "Error: 'position' token count must be 3." << std::endl;
	return false;
      }
    }
    else if (lbl == "velocity") {
      info.velOffset = info.recordSize;
      if (token.count != 3) {
	std::cerr << "Error: 'velocity' token count must be 3." << std::endl;
	return false;
      }
    }
    else if (lbl == "density") {
      info.densityOffset = info.recordSize;
      if (token.count != 1) {
	std::cerr << "Error: 'density' token count must be 1." << std::endl;
	return false;
      }
    }
    else if (lbl == "temperature") {
      info.tempOffset = info.recordSize;
      if (token.count != 1) {
	std::cerr << "Error: 'temperature' token count must be 1." << std::endl;
	return false;
      }
    }
    else if (lbl == "value") {
      info.valOffset = info.recordSize;
      if (token.count != 1) {
	std::cerr << "Error: 'value' token count must be 1." << std::endl;
	return false;
      }
    }
    else if (lbl == "value2") {
      info.val2Offset = info.recordSize;
      if (token.count != 1) {
	std::cerr << "Error: 'value2' token count must be 1." << std::endl;
	return false;
      }
    }
    else if (lbl == "Hsml") {
      info.hsmlOffset = info.recordSize;
      if (token.count != 1) {
	std::cerr << "Error: 'Hsml' token count must be 1." << std::endl;
	return false;
      }
    }
    else if (lbl == "mass") {
      info.massOffset = info.recordSize;
      if (token.count != 1) {
	std::cerr << "Error: 'mass' token count must be 1." << std::endl;
	return false;
      }
    }
    else if (lbl == "type") {
      info.typeOffset = info.recordSize;
      if (token.count != 1) {
	std::cerr << "Error: 'type' token count must be 1." << std::endl;
	return false;
      }
    }
    else if (lbl == "ID") {
      info.IDOffset = info.recordSize;
      if (token.count != 1) {
	std::cerr << "Error: 'ID' token count must be 1." << std::endl;
	return false;
      }
    }

    info.recordSize += token.count * size_item;
  }
  
  if (info.posOffset < 0) {
    std::cerr << "Error: Missing required tokens (position)." << std::endl;
    return false;
  }
  return true;
}


bool FileInfo::loadSingleFile(int fileNumber, TrackingVector<ParticleData>& particles, HeaderInfo &hdr) {
  hdr.UnitLength_in_cm = UnitLength_in_cm;
  hdr.UnitMass_in_g = UnitMass_in_g;
  hdr.UnitVelocity_in_cm_per_s = UnitVelocity_in_cm_per_s;
  hdr.HubbleParam = Hubble;
  
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

  FormatInfo fmt;
  std::unique_ptr<IParticleReader> reader;
  
  switch (readFileFormat) {
  case FileFormat::Auto:  
    // 2) 拡張子／useHDF5 フラグでリーダーの種類を決定
#ifdef HAVE_HDF5
    if (ext == ".h5" || ext == ".hdf5") {
      fmt.tokens = formatTokens;
      reader = std::make_unique<HDF5ParticleReader>(fmt);
    }
#endif   
    
    if (!reader) {
      // ネイティブバイナリ or mmap
      // 3) バイナリ系だけここでフォーマット解析

      int iter = 0;
      const int iter_max = 20;      
      while(iter < iter_max){	
	if (!computeFormatInfo(formatTokens, fmt)) {
	  std::cerr<<"failed to read format info\n";
	  return false;
	}
	
	if (USE_MMAP) {
	  reader = std::make_unique<MMapParticleReader>(fmt);
	} else {
	  reader = std::make_unique<BinaryParticleReader>(fmt);
	}

	bool flag_success = reader->check(fullPath, hdr);
	if(flag_success)
	  break;

	bool has_dummy = false;
	for(size_t i=0;i<formatTokens.size();i++){
	  if(std::strcmp(formatTokens[i].label, "dummy") == 0){
	    has_dummy = true;
	    if(iter == 0){
	      formatTokens[i].count = 0;
	    }else{
	      formatTokens[i].count++;
	      break;
	    }
	    
	  }
	}

	if (!has_dummy) {
	  std::cerr << "There is no label named dummy. No room for the adjustment\n";
	  return false;
	  break;
	}

	printf("iter=%d failed to read the file...\n", iter);	
	iter++;
      }

      if(iter >= iter_max){
	printf("Too many iterations.\n");
	return false;
      }
    }
    break;
  

#ifdef HAVE_HDF5
  case FileFormat::HDF5: {
    fmt.tokens = formatTokens;
    reader = std::make_unique<HDF5ParticleReader>(fmt);
    break;
  }    
#endif
  case FileFormat::Binary: {
    if (!computeFormatInfo(formatTokens, fmt)) {
      std::cerr<<"フォーマット情報解析失敗\n";
      return false;
    }
    
    if (USE_MMAP) {
      reader = std::make_unique<MMapParticleReader>(fmt);
    }else{
      reader = std::make_unique<BinaryParticleReader>(fmt);
    }
    break;
  }
  case FileFormat::Gadget: {
    fmt.tokens = formatTokens;
    reader = std::make_unique<BlockwiseParticleReader>(fmt);    
    break;
  }
  case FileFormat::Framed: {
    reader = std::make_unique<FramedBinaryParticleReader>();
    break;
  }
  default: {
    std::cerr<<"Unknown FileFormat\n";
    return false;
  }
  }
  
  // 4) open / readNext ループ / close
  if (!reader->open(fullPath, hdr)) {
    std::cerr<<"ファイルオープン失敗: "<< fullPath << "\n";
    return false;
  }
  particles.clear();
  ParticleData p;
  while (reader->readNext(p)) {
    particles.push_back(p);
  }
  reader->close();
	
  return true;
}

void ParticleArray::swap_particles(TrackingVector<TrackingVector<ParticleData>>& batchP, int ibatch, HeaderInfo header, int flag_reset){
  float newParticleValueMin[4][6];
  float newParticleValueMax[4][6];
  float maxVal = 0.0f;
  
  TrackingVector<ParticleData>& newParticles = batchP[ibatch];
  
  if (!newParticles.empty()) {    
    int npart_type[6] = {0,0,0,0,0,0};	  

    float tempMin[4][6], tempMax[4][6];
    for(int icolor=0;icolor<4;icolor++){
      for(int i=0;i<6;i++){
	tempMin[icolor][i] = std::numeric_limits<float>::max();
	tempMax[icolor][i] = -std::numeric_limits<float>::max();
      }
    }
  
    // 1ループで maxVal, 座標スケーリング、min/max 更新をすべて実行
#pragma omp parallel
    {
      float localMax = 0.0f;
      float localMin[4][6], localMaxV[4][6];
      int local_npart_type[6] = {0};
      
      // スレッドローカル初期化
      for (int c = 0; c < 4; ++c)
	for (int t = 0; t < 6; ++t) {
	  localMin[c][t] = std::numeric_limits<float>::max();
	  localMaxV[c][t] = -std::numeric_limits<float>::max();
	}
      
#pragma omp for
      for (int i = 0; i < (int)newParticles.size(); ++i) {
	ParticleData& p = newParticles[i];
	int type = p.type;
	
	float m = std::max(std::fabs(p.original_pos[0]),
			   std::max(std::fabs(p.original_pos[1]), std::fabs(p.original_pos[2])));
	if (m > localMax) localMax = m;

	local_npart_type[type]++;

	for (int k = 0; k < 3; ++k)
	  p.pos[k] = p.original_pos[k];  // スケーリングは後でまとめて行う
	p.Hsml = p.originalHsml;

	// getValue を使わず手動展開（高速化のため）
	float values[4] = {p.density, p.temperature, p.val, p.val2};

	for (int c = 0; c < 4; ++c) {
	  float v = values[c];
	  localMin[c][type] = std::min(localMin[c][type], v);
	  localMaxV[c][type] = std::max(localMaxV[c][type], v);
	}
      }

#pragma omp critical
      {
	if (localMax > maxVal) maxVal = localMax;

	for (int t = 0; t < 6; ++t) npart_type[t] += local_npart_type[t];
	
	for (int c = 0; c < 4; ++c)
	  for (int t = 0; t < 6; ++t) {
	    tempMin[c][t] = std::min(tempMin[c][t], localMin[c][t]);
	    tempMax[c][t] = std::max(tempMax[c][t], localMaxV[c][t]);
	  }
      }
    }  // end omp parallel    
  

    // スケーリングを別途行う（maxValが確定してから）
    float invMaxVal = desiredMax / maxVal;
#pragma omp parallel for
    for (int i = 0; i < (int)newParticles.size(); ++i) {
      ParticleData& p = newParticles[i];
      for (int k = 0; k < 3; ++k)
	p.pos[k] *= invMaxVal;
      p.Hsml *= invMaxVal;
    }

    // 最終的な min/max を格納
    for (int c = 0; c < 4; ++c) {
      for (int t = 0; t < 6; ++t) {
	if (npart_type[t] == 0)
	  newParticleValueMin[c][t] = newParticleValueMax[c][t] = 0.0f;
	else {
	  newParticleValueMin[c][t] = tempMin[c][t];
	  newParticleValueMax[c][t] = tempMax[c][t];
	}
      }
    }
  }

  if(flag_reset == 0)
    batchP[particles_index] = std::move(particles);

  particles = std::move(newParticles);
  particles_index = ibatch;

  for(int icolor=0;icolor<4;icolor++){
    for(int i=0;i<6;i++){
      particleValueMin[icolor][i] = newParticleValueMin[icolor][i];
      particleValueMax[icolor][i] = newParticleValueMax[icolor][i];
    }
  }
  
  originalMax = maxVal;
  Header = header;

  if(Header.flag_hdf5 == true){
    UnitLength_in_pc   = Header.UnitLength_in_cm / 3.08e18;;
    UnitMass_in_msolar = Header.UnitMass_in_g / 1.998e33;    
    Hubble             = Header.HubbleParam;
    useComovingCorrdinate = Header.flag_comoving;
    
    setUnits();
  }
  
  particlesDirty = true;  // グローバルなフラグをtrueに設定
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


bool ParticleArray::findParticleID(int ID, float *pos){
  bool flag = false;
  for (size_t i=0;i<particles.size();i++)
    {
      const ParticleData& p = particles[i];
      int ID0 = p.ID;
      if(ID == ID0){
	flag = true;
	pos[0] = p.pos[0];
	pos[1] = p.pos[1];
	pos[2] = p.pos[2];
      }
    }

  return flag;
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
void ParticleArray::computeStellarDensity(int type)
{
  const int N_neighbours = 32;

  bool flag_star = false;
  if(type == 3)
    flag_star = true;
    
  // type >= 3 の粒子のみを抽出
  TrackingVector<starParticle> filtered;
  for (size_t i=0;i<particles.size();i++)
    {
      const ParticleData& p = particles[i];
      if(flag_star){
	if(p.type < 3)
	  continue;
      }else{
	if(p.type != type)
	  continue;
      }


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
    double cosmofac = 1.;
    if(useComovingCorrdinate)
      cosmofac = Header.time;
    
    int original_index = cloud.particles[i].index;
    if(flag_star)
      particles[original_index].density = totalMass * UnitMass_in_msolar
	/ area / std::pow(originalMax / desiredMax * cosmofac * UnitLength_in_pc, 2.) * Hubble;
    else
      particles[original_index].density = density / std::pow(originalMax / desiredMax * cosmofac * UnitLength_in_cm, 3.) * Hubble * Hubble;
    
    printf("i=%d mass=%g h=%g desnity=%g\n", original_index, totalMass, h, particles[original_index].density);
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
  else if (var == "val")
    return val;
  else if (var == "val2")
    return val2;
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
