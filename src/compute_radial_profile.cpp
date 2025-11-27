#include "main.h"
#include "compute_radial_profile.h"

#include <imgui.h>
#include "implot.h"


std::tuple<TrackingVector<float>, TrackingVector<float>>
radialProfile::computeRadialProfile(TrackingVector<ParticleData>& originalParticles,
				    bool useOriginal, bool useLog, int bins, const std::string &var,
				    bool autorange, float& xmin, float& xmax, float& ymin, float& ymax, float& rmax) {
  TrackingVector<float> profile(bins, 0.0f);
  TrackingVector<float> x_coord(bins, 0.0f);
  TrackingVector<int> counts(bins, 0);
  TrackingVector<float> masses(bins, 0.0f);
  
  // もし original 座標を用いるなら、GUIでは normalized 座標が表示されているので、
  // 入力は camCtx.cameraTarget の normalized 値なので、元に戻すために除算する（normalize 時の倍率）

  float normalizationFactor = 1.;
  if(originalParticles.size() > 0)
    normalizationFactor = originalParticles[0].pos[0] / originalParticles[0].original_pos[0];
  
  glm::vec3 center = useOriginal ? (camCenter / normalizationFactor) : camCenter;

  if(var == "v_rad" || var == "mdot"){
    float dmin = std::numeric_limits<float>::max();
    for (const ParticleData &p : originalParticles) {
      glm::vec3 pos = useOriginal ? glm::vec3(p.original_pos[0], p.original_pos[1], p.original_pos[2])
	: glm::vec3(p.pos[0], p.pos[1], p.pos[2]);
      float d = glm::length(pos - center);
      if(d < dmin){
	dmin = d;
	v_center = glm::vec3(p.vel[0], p.vel[1], p.vel[2]);
      }
    }
    printf("v_center=%g %g %g\n", v_center.x, v_center.y, v_center.z);
  }

  printf("center=%g %g %g camCenter=%g %g %g normali=%g\n", center.x, center.y, center.z, this->camCenter.x, this->camCenter.y, this->camCenter.z, normalizationFactor);


  
  if(autorange){
    float maxDist = 0.0f;
    float minDist = std::numeric_limits<float>::max();

    // 最大距離を計算

    bool flag_rmax = (rmax > 0.) ? true : false;

    if(flag_rmax){
      xmax = rmax;
      minDist = rmax;
    }
    
    for (const ParticleData &p : originalParticles) {
      if(p.type != 0)
	continue;
    
      glm::vec3 pos = useOriginal ? glm::vec3(p.original_pos[0], p.original_pos[1], p.original_pos[2])
	: glm::vec3(p.pos[0], p.pos[1], p.pos[2]);
      float d = glm::length(pos - center);
      
      if(flag_rmax && (d > rmax))
	continue;

      if(flag_rmax == false)
	maxDist = std::max(maxDist, d);

      minDist = std::min(minDist, d);

      float val = 0.;
      if(var == "v_rad" || var == "mdot"){
	val = (p.vel[0]-v_center.x) * (pos.x - center.x) + (p.vel[1]-v_center.y) * (pos.y - center.y) + (p.vel[2]-v_center.z) * (pos.z - center.z);
      }else{	
	val = p.getValue(var);
      }
    }
    // 各粒子をビンに分けて平均値を算出

    if(flag_rmax == false)
      xmax = maxDist;

    xmin = minDist;
    if(plotXAxisLog == true)
      if(xmin < 1.e-6 * xmax)
	xmin = 1.e-6 * xmax;        
  }

  bool flag_cumulative = (var == "Mass");
    
  double dr = (xmax - xmin)/bins;
  double dln_r= log(xmax / xmin)/bins;  
  
  for (const ParticleData &p : originalParticles) {
    if(p.type != 0)
      continue;
    
    glm::vec3 pos = useOriginal ? glm::vec3(p.original_pos[0], p.original_pos[1], p.original_pos[2])
      : glm::vec3(p.pos[0], p.pos[1], p.pos[2]);
    float d = glm::length(pos - center);

    if(d > xmax)
      continue;

    if(d < xmin && flag_cumulative == 0)
      continue;
      
    int bin;
    if(plotXAxisLog == false)
      bin = int(d/dr);
    else
      bin = int(log(d / xmin) / dln_r);
    
    if (bin >= bins) bin = bins - 1;
    if (bin < 0) bin = 0;

    //    if(counts[bin]%100 == 0)
    //printf("d=%g pos=%g %g %g center=%g %g %g bin=%d\n", d, pos.x, pos.y, pos.z, center.x, center.y, center.z, bin);
    
    float val = 0.;
    if(var == "v_rad" || var == "mdot"){
      val = (p.vel[0]-v_center.x) * (pos.x - center.x) + (p.vel[1]-v_center.y) * (pos.y - center.y) + (p.vel[2]-v_center.z) * (pos.z - center.z);
    }else{	
      val = p.getValue(var);
    }
    
    profile[bin] += val * p.mass;
    masses[bin] += p.mass;
    counts[bin]++;
  }
  
  if (flag_cumulative) {
    double run = 0.0;
    for (int i = 0; i < bins; ++i) {
      run += masses[i];
      profile[i] = run;
    }
  }else{
    double x_old = 0.;
    for (int i = 0; i < bins; ++i){
      if(var == "mdot"){
	double x_i;
	if(plotXAxisLog == false)    
	  x_i = dr * i;
	else
	  x_i = xmin * exp(dln_r * i);

	double dr = x_i - x_old;
	double mv = profile[i];
	double mdot = mv / dr * UnitMass_in_msolar / UnitTime_in_yr;

	profile[i] = mdot;	
	x_old = x_i;
      }else{
	if (counts[i] > 0)
	  profile[i] /= masses[i];
      }

      if(flagAbsolute){
	if(profile[i] < 0)
	  profile[i] *= -1;
      }
    }
  }

  double ymax0 = -std::numeric_limits<float>::max();
  double ymin0 = std::numeric_limits<float>::max();
  for (int i = 0; i < bins; i++) {
    if(plotXAxisLog == false)    
      x_coord[i] = dr * i;
    else
      x_coord[i] = xmin * exp(dln_r * i);      

    printf("[%d] count=%d val=%g mass=%g\n", i, counts[i], profile[i], masses[i]);
    
    if(counts[i] == 0)
      continue;
    
    if(profile[i] > ymax0)
      ymax0 = profile[i];

    if(profile[i] < ymin0)
      ymin0 = profile[i];    
  }

  if(autorange){
    ymax = ymax0;
    ymin = ymin0;
  }
  
  return std::make_tuple(std::move(x_coord), std::move(profile));
}



void radialProfile::ShowRadialProfileUI(TrackingVector<ParticleData>& part, double unitmass_in_g, double unitlength_in_cm, double unittime_in_s) {
  if (!showWindowRadialProfile) return;
  
  setUnits(unitmass_in_g, unitlength_in_cm, unittime_in_s);
  
  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);  
  ImGui::Begin("Radial Profile", &showWindowRadialProfile, ImGuiWindowFlags_None);
  
  const char* quantities[] = { "Density", "Temperature", "val", "val2", "Hsml", "Mass", "v_rad", "mdot" };
  // 各軸に使う変数のインデックス（デフォルトでは X 軸に "x"、Y 軸に "y" を選択）
  static int selectedVar = 0;
  
  ImGui::Combo("Quantity", &selectedVar, quantities, IM_ARRAYSIZE(quantities));  
  std::string var = quantities[selectedVar];
    
  ImGui::InputInt("Number of Bins", &radialProfileBins);  
  ImGui::Checkbox("Use Original Coordinates", &radialProfileUseOriginal);
  ImGui::Checkbox("Log X Axis", &plotXAxisLog);
  ImGui::Checkbox("Log Y Axis", &plotYAxisLog);

  // 自動レンジを使うかどうかのチェックボックス

  ImGui::Checkbox("Auto Range", &autoRange);
  ImGui::Checkbox("Take absolute value", &flagAbsolute);
  
  // 手動レンジ入力用（autoRange==false の場合）
  if (!autoRange)
    {
      ImGui::InputFloat("X Axis Min", &xmin, 0.0f, 0.0f, "%g");
      ImGui::InputFloat("X Axis Max", &xmax, 0.0f, 0.0f, "%g");
      ImGui::InputFloat("Y Axis Min", &ymin, 0.0f, 0.0f, "%g");
      ImGui::InputFloat("Y Axis Max", &ymax, 0.0f, 0.0f, "%g");
    }  
  
  ImGui::InputFloat("Maximum Radius", &rmax, 0.0f, 0.0f, "%g");
  
  if (ImGui::Button("Compute radial profile")){
    std::tie(radialProfileDataCoord, radialProfileDataValue)
      = computeRadialProfile(part, radialProfileUseOriginal, plotXAxisLog, radialProfileBins, var, autoRange, xmin, xmax, ymin, ymax, rmax);

    for(int i=0;i<radialProfileDataValue.size();i++)
      printf("%g: %g\n", radialProfileDataCoord[i], radialProfileDataValue[i]);

    printf("xmin=%g xmax=%g ymin=%g ymax=%g\n", xmin, xmax, ymin, ymax);
    
    radialProfileComputed = true;
  }


  if(radialProfileComputed){
    if (ImPlot::BeginPlot("Radial Profile", ImVec2(-1, 300))) {
      ImPlot::SetupAxes("Radius", "Value");
      if (plotXAxisLog) ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
      if (plotYAxisLog) ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
      ImPlot::SetupAxisLimits(ImAxis_X1, xmin, xmax, ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, ymin, ymax, ImGuiCond_Always);
            
      ImPlot::PlotLine("Profile", radialProfileDataCoord.data(), radialProfileDataValue.data(), radialProfileDataValue.size());
      ImPlot::EndPlot();
    }
  }

  ImGui::End();
}
