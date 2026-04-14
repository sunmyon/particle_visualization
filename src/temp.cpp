
    
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
