
    
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
