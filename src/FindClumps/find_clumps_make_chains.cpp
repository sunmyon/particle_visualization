#include "render/colormap_defs.h"
#include "FindClumps/find_clumps.h"
#include "FindClumps/find_clumps_IO.h"
#include "make_2D_projection_map.h"

#include "FileIO/file_io.h"
#include "interaction/camera.h"
#include "data/clump_loader.h"

#include <imgui.h>

#include "implot.h"

#ifdef CLUMP_DATA_READ
void FindClump::ReadAndShowClumpsUI(ParticleArray *P, int currentFileIndex, const SnapshotSource& src, CameraContext& cam) {
  if (!showWindowClumpList) return;

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);  
  ImGui::Begin("Clump lists", &showWindowClumpList, ImGuiWindowFlags_None);

  static char buf[255];
  ImGui::InputText("File name of clumpList", buf, IM_ARRAYSIZE(buf));    

  ImGui::SameLine();
  if (ImGui::Button("path")) {
    strcpy(buf, src.folderPath);
  }

  if(P->flag_renew_clumpList){
    selectedClumpID = -1;
    if(P->flag_follow_clump_center){
      for (size_t i = 0; i < P->Clumps.size(); i++){
	if(P->TargetClumpID == P->Clumps[i].clumpID){
	  selectedClumpID = i;
	  break;
	}
      }	
    }

    P->flag_renew_clumpList = false;
  }

  static bool flagReadClumpFile = false;
  if(ImGui::Button("read clump list")){
    P->fname_clump_file = buf;

    auto clumps = loadClumpData(P->fname_clump_file.c_str(),
				currentFileIndex,
				P->desiredMax,
				P->originalMax);

    if(!clumps.empty()){
      P->Clumps = std::move(clumps);
      showEvolve.resize(P->Clumps.size(), false);
      flagReadClumpFile = true;
    }
  }

  if(ImGui::Button("follow clump center") && selectedClumpID >= 0){
    P->TargetClumpID = selectedClumpID;
    P->flag_follow_clump_center = true;
    P->flag_follow_particle_ID = false;
  }
  
  if(flagReadClumpFile){
    if (ImGui::BeginTable("ClumpTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Clump Info", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Select", ImGuiTableColumnFlags_WidthFixed, 50.0f);
      ImGui::TableSetupColumn("Evolve", ImGuiTableColumnFlags_WidthFixed, 50.0f);
	      
      ImGui::TableHeadersRow();

      // 各クランプ情報を表示
      for (size_t i = 0; i < P->Clumps.size(); i++) {
	ClumpData cp = P->Clumps[i];

	// クランプの情報文字列を作成
        char label[256];
        snprintf(label, sizeof(label), "%4zu   %4d    %g  %g  (%.3f, %.3f, %.3f)  %d  %.3g %d",
                 i, cp.count, cp.mass, cp.density, cp.Pos[0], cp.Pos[1], cp.Pos[2], cp.stellar_count, cp.stellar_mass, cp.stellar_id);

        ImGui::TableNextRow();

        // 左側の列：クランプ情報を表示（Selectableでも良い）
        ImGui::TableSetColumnIndex(0);
        if (ImGui::Selectable(label, false)) {
          // 行全体がクリックされた場合、カメラ移動などの処理
          float dist = glm::length(cam.cameraPos - cam.cameraTarget);
          glm::vec3 direction = cam.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
          cam.cameraTarget = glm::vec3(cp.Pos[0], cp.Pos[1], cp.Pos[2]);
          cam.cameraPos = cam.cameraTarget - direction * dist;
        }

        // 右側の列：ラジオボタンで選択
        ImGui::TableSetColumnIndex(1);
        if (ImGui::RadioButton(("##clumpSel" + std::to_string(i)).c_str(), selectedClumpID == static_cast<int>(i))) {
          selectedClumpID = static_cast<int>(i);
        }

	ImGui::TableSetColumnIndex(2);
	// チェックボックスのラベルは "##evolve" + i など、ユニークにする
	std::string checkboxLabel = "##evolve" + std::to_string(i);

	bool tmp = showEvolve[i];
	if (ImGui::Checkbox(checkboxLabel.c_str(), &tmp)) {
	  showEvolve[i] = tmp;
	}
      }
      ImGui::EndTable();
    }
  }

  static int finalFileIndex = 1000, dsnapshot = 10;
  ImGui::InputInt("final snapshot index", &finalFileIndex);
  ImGui::InputInt("snapshot interval", &dsnapshot);

  static bool flagAutoRangeX = true;
  ImGui::Checkbox("Auto Range time", &flagAutoRangeX);

  static float t_min_input = 0., t_max_input = 1.;
  if(flagAutoRangeX == false){
    ImGui::InputFloat("time Min", &t_min_input, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("time Max", &t_max_input, 0.0f, 0.0f, "%g");
  }
  
  const char* quantities[] = { "Density", "Temperature", "ClumpMass", "StellarMass"};
  // 各軸に使う変数のインデックス（デフォルトでは X 軸に "x"、Y 軸に "y" を選択）
  static int selectedVar = 0;
  
  ImGui::Combo("Quantity", &selectedVar, quantities, IM_ARRAYSIZE(quantities));  
  std::string var = quantities[selectedVar];

  static bool useLogScale = true;
  ImGui::Checkbox("Use Log scale Y", &useLogScale);

  static bool flagAutoRangeY = true;
  ImGui::Checkbox("Auto Range for value (Y-axis)", &flagAutoRangeY);

  static float val_min_input = 1., val_max_input = 1.e10;
  if(flagAutoRangeY == false){
    ImGui::InputFloat("val Min", &val_min_input, 0.0f, 0.0f, "%g");
    ImGui::InputFloat("val Max", &val_max_input, 0.0f, 0.0f, "%g");
  }

  static bool flagUpdateClumpCache = false;
  if(ImGui::Button("Plot Clump Evolution")){
    flagShowClumpEvolution = true;
    flagUpdateClumpCache = true;
  }

  static float t_min, t_max;
  static float val_min, val_max;

  struct ClumpEvolutionCache {
    TrackingVector<float> timeFloats;   // 時刻 (float)
    TrackingVector<float> valueFloats;  // 変数値 (float)
    int index;
  };

  static TrackingVector<ClumpEvolutionCache> g_evolutionCache;
  
  if(flagUpdateClumpCache){
    g_evolutionCache.clear();

    t_min =  std::numeric_limits<float>::infinity();
    t_max = -std::numeric_limits<float>::infinity();
    val_min =  std::numeric_limits<float>::infinity();
    val_max = -std::numeric_limits<float>::infinity();
    
    for(size_t i=0;i<P->Clumps.size();i++){
      if(!showEvolve[i])
	continue;
      
      TrackingVector<float> times;
      TrackingVector<ClumpData> clumps;
      readClumpEvolution(P->fname_clump_file, currentFileIndex, finalFileIndex, dsnapshot,P->Clumps[i].clumpID, times, clumps);      
	
      // times と clumps のサイズが一致し、かつデータが存在することを確認
      if(times.empty() || times.size() != clumps.size())
	continue;

      ClumpEvolutionCache cache;
      cache.index = static_cast<int>(i);
      cache.timeFloats.resize(times.size());
      cache.valueFloats.resize(times.size());
      for (size_t j = 0; j < times.size(); ++j) {
	float t = static_cast<float>(times[j]);
	float v = clumps[j].getValue(var);

	cache.timeFloats[j]  = t;
	if (t < t_min)   t_min   = t;
	if (t > t_max)   t_max   = t;

	cache.valueFloats[j] = v;
	if(useLogScale && v == 0.)
	  continue;

	if (v < val_min) val_min = v;
	if (v > val_max) val_max = v;
      }
      
      g_evolutionCache.push_back(std::move(cache));      
    }

    if(flagAutoRangeX){
      t_min_input = t_min;
      t_max_input = t_max;
    }

    if(flagAutoRangeY){
      val_min_input = val_min;
      val_max_input = val_max;
    }      
      
    flagUpdateClumpCache = false;
  }
  
  if(flagShowClumpEvolution){
    if (ImPlot::BeginPlot("Time Evolution", ImVec2(-1, 300), ImPlotFlags_None))
      {	    
	ImPlot::SetupAxis(ImAxis_X1, "Time", ImPlotAxisFlags_None);
	ImPlot::SetupAxis(ImAxis_Y1, var.c_str(), ImPlotAxisFlags_None);
      
	if (useLogScale)
	  ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
      	
	if (flagAutoRangeX && (t_min < t_max)){
	  ImPlot::SetupAxisLimits(ImAxis_X1, t_min, t_max, ImGuiCond_Always);
	}else{
	  ImPlot::SetupAxisLimits(ImAxis_X1, t_min_input, t_max_input, ImGuiCond_Always);
	}
	
	if(flagAutoRangeY){
	  ImPlot::SetupAxisLimits(ImAxis_Y1, val_min, val_max, ImGuiCond_Always);
	}else{
	  ImPlot::SetupAxisLimits(ImAxis_Y1, val_min_input, val_max_input, ImGuiCond_Always);
	}
	
	for(size_t i=0;i<g_evolutionCache.size();i++){
	  auto& cache = g_evolutionCache[i];
	  int index = cache.index;
	  std::string label = "Clump " + std::to_string(P->Clumps[index].clumpID);

	  ImPlot::PlotLine(label.c_str(), cache.timeFloats.data(), cache.valueFloats.data(), static_cast<int>(cache.timeFloats.size()));
	}
	ImPlot::EndPlot();
      }
  }
  
  ImGui::End();
}



void FindClump::give_stellar_id_to_clumps(int initstep, int nsnapshots, int dstep, std::string fname){
  TrackingVector<TrackingVector<clump_evolution_info>> clumps_all(nsnapshots);
  
  for(int isnap=0;isnap<nsnapshots;isnap++){
    int fileindex = initstep + isnap * dstep;

    uint32_t mask = (L_CLUMP_ID | L_CLUMP_NEXT_ID | L_CLUMP_SIZE | L_CLUMP_OFFSET | L_CLUMP_STELLAR_COUNT);
    ClumpInfoIO in;
    
    bool flag = ClumpIO::readSnapshot(fname, fileindex, mask, in);

    if(flag == false)
      continue;    

    size_t nClumps = in.clump_id.size();
    clumps_all[isnap].resize(nClumps);

    bool flag_star = false;
    for(size_t i=0;i<in.clump_id.size();i++){      
      clumps_all[isnap][i].size = in.clump_size[i];
      clumps_all[isnap][i].offset = in.clump_offset[i];
      clumps_all[isnap][i].index = in.clump_id[i];
      if(i < in.clump_next_id.size())
	clumps_all[isnap][i].next_index = in.clump_next_id[i];
      
      clumps_all[isnap][i].stellar_count = in.clump_stellar_count[i];
      clumps_all[isnap][i].flag_star = false; //just initial flag
      clumps_all[isnap][i].stellar_id = -1; //initialization
      
      if(in.clump_stellar_count[i] > 0)
	flag_star = true;
    }
   
    if(flag_star){
      uint32_t mask_part = L_ALL_PARTICLE_FIELDS;
      bool flag = ClumpIO::readSnapshot(fname, fileindex, mask_part, in);

      if(flag == false)
	printf("Why this happen? We could not read the file, %s\n", fname.c_str());      
      
      for(size_t i=0;i<in.clump_id.size();i++){      
	if(in.clump_stellar_count[i] == 0)
	  continue;

	int ID_min = -1;
	bool flag_first = true;

	int index_init = in.clump_offset[i];
	int len = in.clump_size[i];
	for(int ip=0;ip < len;ip++){
	  int index = index_init + ip;
	  if(in.particle_type[index] < 3)
	    continue;

	  if(flag_first){
	    ID_min = in.particle_ids[index];
	    flag_first = false;
	  }
	    
	  if(ID_min > in.particle_ids[index])
	    ID_min = in.particle_ids[index];
	}

	clumps_all[isnap][i].stellar_id = ID_min;
      }      
    }      
  }  

  for(int isnap=0;isnap<nsnapshots;isnap++){
    for(size_t ic=0;ic<clumps_all[isnap].size();ic++){
      if(clumps_all[isnap][ic].stellar_id >= 0)
	continue;
      
      if(clumps_all[isnap][ic].flag_star)
	continue;

      int stellar_id = -1;
      int next_index = clumps_all[isnap][ic].next_index;
      for(int jsnap=isnap+1;jsnap<nsnapshots;jsnap++){
	if(next_index < 0)
	  break;

	int stellar_count = clumps_all[jsnap][next_index].stellar_count;
	if(stellar_count > 0){
	  stellar_id = clumps_all[jsnap][next_index].stellar_id;
	  break;
	}
	
	next_index = clumps_all[jsnap][next_index].next_index;
      }

      clumps_all[isnap][ic].stellar_id = stellar_id;
      clumps_all[isnap][ic].flag_star = true;
      
      next_index = clumps_all[isnap][ic].next_index;      
      for(int jsnap=isnap+1;jsnap<nsnapshots;jsnap++){
	if(next_index < 0)
	  break;

	clumps_all[jsnap][next_index].stellar_id = stellar_id;
	clumps_all[jsnap][next_index].flag_star = true;
	
	int stellar_count = clumps_all[jsnap][next_index].stellar_count;
	if(stellar_count > 0)
	  break;
		
	next_index = clumps_all[jsnap][next_index].next_index;
      }      
    }
  }
  
  for(int isnap=0;isnap<nsnapshots;isnap++){
    int fileindex = initstep + isnap * dstep;

    ClumpInfoIO out;
    size_t nClumps = clumps_all[isnap].size();
    out.clump_stellar_id.resize(nClumps);
    
    for(size_t i=0;i<clumps_all[isnap].size();i++)
      out.clump_stellar_id[i] = clumps_all[isnap][i].stellar_id;

    uint32_t mask_out = L_CLUMP_STELLAR_ID;
    ClumpIO::writeSnapshot(fname, fileindex, mask_out, out);    
  }
}


TrackingVector<TrackingVector<FindClump::clump_evolution_info *>>
FindClump::make_clump_evolution_chain(int initstep, int nsnapshots, int dstep, std::string fname){
  clumpLists.resize(nsnapshots);
  
  for(int isnap=0;isnap<nsnapshots;isnap++){
    int fileindex = initstep + isnap * dstep;

    uint32_t mask = (L_TIME | L_CLUMP_ID | L_CLUMP_NEXT_ID | L_CLUMP_SIZE | L_CLUMP_OFFSET
		     | L_CLUMP_STELLAR_COUNT | L_CLUMP_STELLAR_ID
		     | L_CLUMP_STELLAR_MASS | L_CLUMP_STELLAR_MASS_MAXIMUM | L_CLUMP_POSITION | L_CLUMP_DENSITY
		     | L_CLUMP_TEMPERATURE | L_CLUMP_TEMP_DENSITY_WEIGHTED | L_CLUMP_MASS);
    ClumpInfoIO in;
    
    bool flag = ClumpIO::readSnapshot(fname, fileindex, mask, in);

    if(flag == false)
      continue;    

    size_t nClumps = in.clump_id.size();
    clumpLists[isnap].resize(nClumps);
    
    for(size_t i=0;i<in.clump_id.size();i++){      
      clumpLists[isnap][i].size = in.clump_size[i];
      clumpLists[isnap][i].offset = in.clump_offset[i];

      clumpLists[isnap][i].index = in.clump_id[i];
      if(in.clump_next_id.size())
	clumpLists[isnap][i].next_index = in.clump_next_id[i];
      
      clumpLists[isnap][i].stellar_count = in.clump_stellar_count[i];
      clumpLists[isnap][i].stellar_id = in.clump_stellar_id[i];

      clumpLists[isnap][i].pos[0] = in.clump_position[3*i+0];
      clumpLists[isnap][i].pos[1] = in.clump_position[3*i+1];
      clumpLists[isnap][i].pos[2] = in.clump_position[3*i+2];
      
      clumpLists[isnap][i].mass = in.clump_mass[i];
      clumpLists[isnap][i].density = in.clump_density[i];
      clumpLists[isnap][i].temperature = in.clump_temperature[i];
      clumpLists[isnap][i].temperature_d = in.clump_temperature_density_weighted[i];

      clumpLists[isnap][i].stellar_mass = in.clump_stellar_mass[i];
      clumpLists[isnap][i].stellar_mass_maximum = in.clump_stellar_mass_maximum[i];
      
      clumpLists[isnap][i].flag_star = false; //just flag

      clumpLists[isnap][i].snapindex = isnap;
      clumpLists[isnap][i].time = in.time;
    } 
  }  
  
  int global_id = 0;
  TrackingVector<TrackingVector<clump_evolution_info *>> clump_chain_array;
  for(int isnap=0;isnap<nsnapshots;isnap++){
    for(auto &clump : clumpLists[isnap]){
      if(clump.flag_star)
	continue;

      TrackingVector<clump_evolution_info *> clump_chain;
      clump_chain.push_back(&clump);
      clump.flag_star = true;
      
      int next_index = clump.next_index;
      for(int jsnap=isnap+1;jsnap<nsnapshots;jsnap++){
	if(next_index < 0)
	  break;

	clump_evolution_info& clump_next = clumpLists[jsnap][next_index];
	if(clump_next.flag_star){
	  break; // this is merged branch
	}	
	
	clump_chain.push_back(&clump_next);
	clump_next.flag_star = true;
	
	next_index = clump_next.next_index;
      }

      for(auto &clump_member : clump_chain)
	clump_member->global_id = global_id;

      clump_chain_array.push_back(clump_chain);
      global_id++;
    }
  }

  flagClumpChainComputed = true;
  
  return clump_chain_array;
}


TrackingVector<FindClump::clump_properties> FindClump::calc_chain_properties(TrackingVector<TrackingVector<FindClump::clump_evolution_info *>>& clumpChain){
  TrackingVector<clump_properties> cprops;
  TrackingVector<TrackingVector<clump_evolution_info*>> clump_chain_array;
  
  for(size_t i=0;i< clumpChain.size();i++){
    auto &cChain = clumpChain[i];
    
    if(cChain.size() < LENGTH_MINIMUM_CHAIN && cChain.front()->stellar_id == -1){
      //printf("skipped: global_id=%d\n", cChain.front()->global_id);
      continue;
    }

    int next_index_last = cChain.back()->next_index;
    if(next_index_last >= 0)
      continue;
    
    clump_properties ch;
    
    ch.first_snapshot = cChain.front()->snapindex;
    ch.last_snapshot = cChain.back()->snapindex;
    
    ch.first_time = cChain.front()->time;
    ch.last_time = cChain.back()->time;
    
    ch.global_id = cChain.front()->global_id;
    ch.stellar_id = cChain.front()->stellar_id;

    int nstar = 0;
    float mstar = 0.;
    float temperature = 0., temperature_d = 0.;
    float density = 0.;
    float mstar_maximum = 0., mass_maximum = 0.;
    bool flagfirst = true;
    
    for(size_t i=0;i<cChain.size();i++){
      clump_evolution_info *chain = cChain[i];
      
      if(flagfirst && chain->stellar_count > 0){	
	ch.SF_snapshot = chain->snapindex;
	ch.SF_time = chain->time;
	flagfirst = false;
      }

      if(chain->stellar_count > nstar)
	nstar = chain->stellar_count;

      if(chain->stellar_mass > mstar)
	mstar = chain->stellar_mass;

      if(chain->stellar_mass_maximum > mstar_maximum)
	mstar_maximum = chain->stellar_mass_maximum;

      if(chain->mass > mass_maximum)
	mass_maximum = chain->mass;

      if(flagfirst == true){
	temperature = chain->temperature;
	temperature_d = chain->temperature_d;
	density = chain->density;
      }
    }

    ch.nstar = nstar;
    ch.mstar = mstar;
    ch.mstar_maximum = mstar_maximum;
    ch.mass_maximum = mass_maximum;
    ch.density = density;
    ch.temperature = temperature;
    ch.temperature_d = temperature_d;
    
    cprops.push_back(ch);
    clump_chain_array.push_back(cChain);
  }

  clumpChain = clump_chain_array;  
  return cprops;
}


void FindClump::showClumpChainList(ParticleArray *P, ProjectionMapGenerator *proj, FileInfo& fileinfo, CameraContext& cam){
  if(!flagShowWindowClumpChainList)
    return;

  auto& src = fileinfo.getSource();
  
  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);  
  ImGui::Begin("Clump chain lists", &flagShowWindowClumpChainList, ImGuiWindowFlags_None);
  
  if(ImGui::Button("extract clump evolution chain")){
    clumpChain = make_clump_evolution_chain(clumpChainInitFileIndex, clumpChainNsnapshots, clumpChainDFileIndex, clumpChainFileName);
    clumpChainProps = calc_chain_properties(clumpChain);

    double unit_mass_in_msun = P->units.mass_msun / P->units.hubble;
    for(auto &clump : clumpChainProps){
      clump.mstar *= unit_mass_in_msun;
      clump.mstar_maximum *= unit_mass_in_msun;
      clump.mass_maximum *= unit_mass_in_msun;
    }
    
    flagClumpChainComputed = true;
  }
  
  if(flagClumpChainComputed){
    static TrackingVector<bool> plotClumps;
    if (plotClumps.size() != clumpChainProps.size()) {
      plotClumps.resize(clumpChainProps.size(), false);
    }
    
    if (ImGui::BeginTable("ClumpChainTable", 10, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Chain ID", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableSetupColumn("Start Snap", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableSetupColumn("End Snap", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableSetupColumn("Stellar ID", ImGuiTableColumnFlags_WidthFixed, 80);
      ImGui::TableSetupColumn("Stellar Mass", ImGuiTableColumnFlags_WidthFixed, 80);
      ImGui::TableSetupColumn("Stellar Count", ImGuiTableColumnFlags_WidthFixed, 80);
      ImGui::TableSetupColumn("Maximum stellar Mass", ImGuiTableColumnFlags_WidthFixed, 80);
      ImGui::TableSetupColumn("Maximum Clump Mass", ImGuiTableColumnFlags_WidthFixed, 80);
      ImGui::TableSetupColumn("Temperature", ImGuiTableColumnFlags_WidthStretch, 80);
      ImGui::TableSetupColumn("Plot", ImGuiTableColumnFlags_WidthStretch, 50);
      ImGui::TableHeadersRow();

      for(size_t idx = 0; idx < clumpChainProps.size(); idx++){
	auto& ch = clumpChainProps[idx];

	ImGui::TableNextRow();
        ImGui::PushID(idx);
	
        bool is_selected = (static_cast<int>(idx) == selected_chain_index);
	ImGui::TableSetColumnIndex(0);
	if (ImGui::Selectable(("Chain " + std::to_string(ch.global_id)).c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
	  selected_chain_index = idx;
	}
	
        // オプション：行が選択状態なら背景色を変える（Selectable自体で行全体をカバーするため、必要なら TableSetBgColor は使えます）
        if (is_selected) {
	  ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.3f, 0.5f, 0.8f, 0.5f)));
        }
	
	ImGui::TableSetColumnIndex(1);
	ImGui::Text("%d", ch.first_snapshot);
	ImGui::TableSetColumnIndex(2);
	ImGui::Text("%d", ch.last_snapshot);
	ImGui::TableSetColumnIndex(3);
	ImGui::Text("%d", ch.stellar_id);
	ImGui::TableSetColumnIndex(4);
	ImGui::Text("%g", ch.mstar);
	ImGui::TableSetColumnIndex(5);
	ImGui::Text("%d", ch.nstar);
	ImGui::TableSetColumnIndex(6);
	ImGui::Text("%g", ch.mstar_maximum);
	ImGui::TableSetColumnIndex(7);
	ImGui::Text("%g", ch.mass_maximum);
	ImGui::TableSetColumnIndex(8);
	ImGui::Text("%g", ch.temperature_d);

        // 右端の列：Plot用チェックボックス
        ImGui::TableSetColumnIndex(9);
	bool flag = plotClumps[idx];
        if (ImGui::Checkbox(("##plot" + std::to_string(idx)).c_str(), &flag)) {
            // チェックボックスが更新された場合の処理があればここに追加
	  plotClumps[idx] = flag;
        }
	
	ImGui::PopID();
      }
      ImGui::EndTable();	    
    }    
      
    // 読み込みボタン
    ImGui::BeginDisabled(selected_chain_index == -1);
    
    if(selected_chain_index >= 0 && selected_chain_index < static_cast<int>(clumpChainProps.size())){
      const auto& selected_chain = clumpChainProps[selected_chain_index];
      const auto& ch = clumpChain[selected_chain_index];
      
      if(ImGui::Button("Load Selected Chain")){	    
	i_snapshot = 0;
	flag_button_pushed = true;
	flagFileLoaded = true;
      }

      ImGui::SameLine();	  
	    
      if(ImGui::Button("Prev")){
	if(flagFileLoaded && i_snapshot > 0){
	  i_snapshot--;
	  flag_button_pushed = true;
	}
      }

      ImGui::SameLine();
      
      if(ImGui::Button("Next")){
	if(flagFileLoaded && i_snapshot < static_cast<int>(ch.size())-1){
	  i_snapshot++;
	  flag_button_pushed = true;
	}
      }	 

      ImGui::SameLine();
      if(ImGui::Button("from fixed viewpoint")){
	cam.cameraPos = cam.cameraTarget + glm::vec3(0.0f, 0.0f, -1.0f);

	glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f); // 仮のアップベクトル
	glm::vec3 forward = glm::normalize(cam.cameraTarget - cam.cameraPos);       
	glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
	glm::vec3 up = glm::normalize(glm::cross(right, forward));
	
	glm::mat4 viewMatrix = glm::lookAt(cam.cameraPos, cam.cameraTarget, up);
	glm::mat3 rotationMatrix = glm::mat3(viewMatrix);
	cam.distance = glm::length(cam.cameraPos - cam.cameraTarget);
	cam.cameraOrientation = glm::quat_cast(rotationMatrix);
      }
      
      ImGui::Text("current snapshot index: %d (init=%d now=%d step=%d) time=%g\n"
		  , src.initialIndex + (selected_chain.first_snapshot + i_snapshot) * src.skipStep
		  , selected_chain.first_snapshot, selected_chain.first_snapshot + i_snapshot, src.skipStep
		  , P->particleBlock.header.time);
      
      if(flag_button_pushed){
	int snapshot = src.initialIndex + (selected_chain.first_snapshot + i_snapshot) * src.skipStep;

	float pos[3];

	pos[0] = ch[i_snapshot]->pos[0] * P->desiredMax / P->originalMax;
	pos[1] = ch[i_snapshot]->pos[1] * P->desiredMax / P->originalMax;
	pos[2] = ch[i_snapshot]->pos[2] * P->desiredMax / P->originalMax;
	
	fileinfo.loadNewSnapshot(snapshot, P);
	    
	float dist = glm::length(cam.cameraPos - cam.cameraTarget);
	glm::vec3 direction = cam.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
	cam.cameraTarget = glm::vec3(pos[0], pos[1], pos[2]);
	cam.cameraPos = cam.cameraTarget - direction * dist;
	    
	flag_button_pushed = false;
      }
    
      // 物理量に応じてデータを作成
      std::vector<double> times;
      std::vector<double> values;

      const char* quantities[] = { "Density", "Temperature", "ClumpMass", "StellarMass"};
      // 各軸に使う変数のインデックス（デフォルトでは X 軸に "x"、Y 軸に "y" を選択）
      static int selectedVar = 0;
      
      ImGui::Combo("Quantity", &selectedVar, quantities, IM_ARRAYSIZE(quantities));  
      std::string var = quantities[selectedVar];

      // ここでは、例えば "density" を選んだとする
      
      static bool useLogScale = true;
      ImGui::Checkbox("Use Log scale Y", &useLogScale);

      static bool autoScale = true;
      ImGui::Checkbox("Use autoscale", &autoScale);

      static float xmin, xmax, ymin, ymax;      
      if(!autoScale){
	ImGui::InputFloat("X Axis Min", &xmin, 0.0f, 0.0f, "%g");
	ImGui::InputFloat("X Axis Max", &xmax, 0.0f, 0.0f, "%g");
	ImGui::InputFloat("Y Axis Min", &ymin, 0.0f, 0.0f, "%g");
	ImGui::InputFloat("Y Axis Max", &ymax, 0.0f, 0.0f, "%g");
      }
            
      if (ImPlot::BeginPlot("Time Evolution", ImVec2(-1, 300), ImPlotFlags_None))
      {
	ImPlot::SetupAxis(ImAxis_X1, "Time", ImPlotAxisFlags_None);
	ImPlot::SetupAxis(ImAxis_Y1, var.c_str(), ImPlotAxisFlags_None);
	  
	if (useLogScale)
	  ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
	else
	  ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);

	TrackingVector<TrackingVector<float>> times_array, values_array;
	for (size_t i = 0; i < clumpChainProps.size(); i++){
	  if (plotClumps[i] || static_cast<int>(i) == selected_chain_index){
	    const auto& ch = clumpChain[i];

	    TrackingVector<float> times, values;	    
	    for (const auto &snap : ch) {
	      times.push_back(snap->time);
	      values.push_back(snap->getValue(var));
	    }

	    times_array.push_back(times);
	    values_array.push_back(values);
	  }
	}
	
	if(autoScale){
	  float time_max = -1.e20, value_max = -1.e20;
	  float time_min = 1.e20, value_min = 1.e20;	  
	  for(size_t i=0;i<times_array.size();i++){
	    TrackingVector<float>& times = times_array[i];
	    TrackingVector<float>& values = values_array[i];
	    
	    for(size_t j=0;j<times.size();j++){
	      if(time_max < times[j]) time_max = times[j];
	      if(time_min > times[j]) time_min = times[j];

	      if(useLogScale){
		if(values[j] > 0.){	      
		  if(value_max < values[j]) value_max = values[j];
		  if(value_min > values[j]) value_min = values[j];
		}
	      }
	    }
	  }

	  xmax = time_max, xmin = time_min;
	  ymax = value_max, ymin = value_min;
	}

	ImPlot::SetupAxisLimits(ImAxis_X1, xmin, xmax, ImGuiCond_Always);
	ImPlot::SetupAxisLimits(ImAxis_Y1, ymin, ymax, ImGuiCond_Always);
	  
	for(size_t i=0;i<times_array.size();i++){
	  TrackingVector<float>& times = times_array[i];
	  TrackingVector<float>& values = values_array[i];
	  ImPlot::PlotLine(var.c_str(), times.data(), values.data(), times.size());	  	
	}
	
	double currentTime = P->particleBlock.header.time;
	  
        // 現在時刻に対応する縦の破線を描画（例：赤色、太さ 1.0）
        ImU32 red = ImGui::GetColorU32(ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        DrawVerticalDashedLine(currentTime, red, 1.0f, 5.0f, 3.0f);
	
	ImPlot::EndPlot();
      }

      static float len = 1.;
      static float val_min = 1.e2;
      static float val_max = 1.e8;
      static int npixel_input = 400;
      static int nslices = 100;
      static char fdir[255];

      ImGui::PushItemWidth(100); // 幅を 100 ピクセルに設定
      ImGui::InputFloat("len", &len, 0.0f, 0.0f, "%g");
      ImGui::InputFloat("val_min", &val_min, 0.0f, 0.0f, "%g");
      ImGui::SameLine();
      ImGui::InputFloat("val_max", &val_max, 0.0f, 0.0f, "%g");
      ImGui::InputInt("npixel", &npixel_input, 10, 1000);
      ImGui::SameLine();
      ImGui::InputInt("nslices", &nslices, 10, 1000);      
      ImGui::InputText("output directory##evolution_map", fdir, IM_ARRAYSIZE(fdir));
      ImGui::PopItemWidth(); // 幅設定のリセット
      
      const char* quantities2[] = {"Density", "Temperature", "val", "val2", "Hsml", "Mass" };
      static int selectedVar2 = 0;
      ImGui::Combo("Quantity##evo", &selectedVar2, quantities2, IM_ARRAYSIZE(quantities2));
      var = quantities2[selectedVar];
      
      if(ImGui::Button("make projection maps")){
	for(size_t i=0;i<ch.size();i++){
	  int flag_use_amvector = 0;
	  if(i==0)
	    flag_use_amvector = 1;

	  int snapshot = src.initialIndex + (selected_chain.first_snapshot + i) * src.skipStep;	  
	  fileinfo.loadNewSnapshot(snapshot, P);
	
	  float pos_center[3];	  
	  pos_center[0] = ch[i]->pos[0] * P->desiredMax / P->originalMax;
	  pos_center[1] = ch[i]->pos[1] * P->desiredMax / P->originalMax;
	  pos_center[2] = ch[i]->pos[2] * P->desiredMax / P->originalMax;

	  char fname_output[512];
	  snprintf(fname_output, sizeof(fname_output), "%s/image_clump%d_%04zu.png", fdir, selected_chain_index, i);
	  
	  proj->set_projection_parameters(P->particleBlock.particles, flag_use_amvector, pos_center, len, val_min, val_max, npixel_input, nslices, var);
	  proj->make_density_map(P, fname_output);
	}
      }
    }
    ImGui::EndDisabled();
  }
  
  ImGui::End();
}

void FindClump::DrawVerticalDashedLine(double x_value, const ImU32& col, float thickness, float dash_length, float gap_length) {
    // プロット内の Y 軸の表示範囲を取得
    ImPlotRect limits = ImPlot::GetPlotLimits();
    double y_min = limits.Y.Min;
    double y_max = limits.Y.Max;

    // データ空間での端点 (x_value, y_min) と (x_value, y_max) をピクセル座標に変換
    ImVec2 p0 = ImPlot::PlotToPixels(ImVec2(x_value, y_min));
    ImVec2 p1 = ImPlot::PlotToPixels(ImVec2(x_value, y_max));

    // x 座標は固定
    float x_pixel = p0.x;
    
    // 垂直方向のピクセル距離を計算（p1.y > p0.y と仮定）
    float total_length = p1.y - p0.y;    
    if(total_length < 0){
      float tmp = p0.y;
      p0.y = p1.y;
      p1.y = tmp;
      total_length = p1.y - p0.y;    
    }

    ImDrawList* draw_list = ImPlot::GetPlotDrawList();
    if (!draw_list) return; 
    
    float current_y = p0.y;    
    while (current_y < p1.y) {
      float seg_end = current_y + dash_length;
      if (seg_end > p1.y)
	seg_end = p1.y;

      draw_list->AddLine(ImVec2(x_pixel, current_y), ImVec2(x_pixel, seg_end), col, thickness);
      current_y += dash_length + gap_length;
    }
}

float FindClump::readClumpTime(std::string fname, int snapshotIndex){
  uint32_t mask = L_TIME;
  ClumpInfoIO in;    
  bool flag = ClumpIO::readSnapshot(fname, snapshotIndex, mask, in);
  
  if(flag == false){
    return 0.;
  }

  return in.time;
}


void FindClump::readClumpEvolution(std::string fname, int snapshotInit, int snapshotEnd, int dsnapshot, int clumpID_init,
				  TrackingVector<float>& times, TrackingVector<ClumpData>& clumps){
  clumps = {};
  times = {};
  
  int snapshot = snapshotInit;
  int clumpID = clumpID_init;
  while(snapshot <= snapshotEnd){
    uint32_t mask = (L_TIME | L_CLUMP_ID | L_CLUMP_NEXT_ID | L_CLUMP_POSITION | L_CLUMP_DENSITY | L_CLUMP_TEMPERATURE | L_CLUMP_MASS);
    ClumpInfoIO in;    
    bool flag = ClumpIO::readSnapshot(fname, snapshot, mask, in);

    if(flag == false){
      snapshot += dsnapshot;
      continue;
    }

    ClumpData cd;
    bool flag_find_next_clump = false;
    for (size_t i = 0; i < in.clump_id.size(); i++) {
      if(in.clump_id[i] != clumpID)
	continue;

      flag_find_next_clump = true;
      
      cd.clumpID = in.clump_id[i];
      cd.density = in.clump_density[i];
      cd.temperature = in.clump_temperature[i];    
      cd.mass = in.clump_mass[i];
      break;
    }

    if(!flag_find_next_clump)
      break;
    
    times.push_back(in.time);
    clumps.push_back(cd);

    clumpID = cd.clumpID;
    snapshot += dsnapshot;
  }
}


void FindClump::addNextClumpIDtoHDF5(TrackingVector<StructureNode *> nodes,
				     const std::string &filename, int snapshotIndex)
{
  size_t count = 0;
  for (auto& node : nodes) {
    if(node->isLeaf())
      count++;    
  }

  size_t nClumps = count;
  TrackingVector<int> clump_next_id(nClumps);

  for(size_t i=0, count=0;i<nodes.size();i++){
    StructureNode *node = nodes[i];
    if(!node->isLeaf())
      continue;
    
    clump_next_id[count] = node->clumpID_in_next_snapshot;
    count++;
  }
    
  ClumpInfoIO out;
  out.clump_next_id.resize(nClumps);
    
  for(size_t i=0;i<nClumps;i++)
    out.clump_next_id[i] = clump_next_id[i];

  uint32_t mask_out = L_CLUMP_NEXT_ID;
  ClumpIO::writeSnapshot(filename, snapshotIndex, mask_out, out);  
}
#endif
