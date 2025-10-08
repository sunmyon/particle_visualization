#include "main.h"
#include "camera.h"
#include "compute_radial_profile.h"

#include <imgui.h>
#include "implot.h"


std::tuple<TrackingVector<float>, TrackingVector<float>>
radialProfile::computeRadialProfile(TrackingVector<ParticleData>& originalParticles,
				    bool useOriginal, bool useLog, int bins, const std::string &var,
				    bool autorange, float& xmin, float& xmax, float& ymin, float& ymax) {
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

  if(autorange){
    float maxDist = 0.0f;
    float minDist = std::numeric_limits<float>::max();
    float maxvalue = -std::numeric_limits<float>::max();
    float minvalue = std::numeric_limits<float>::max();

    // 最大距離を計算

    for (const ParticleData &p : originalParticles) {
      if(p.type != 0)
	continue;
    
      glm::vec3 pos = useOriginal ? glm::vec3(p.original_pos[0], p.original_pos[1], p.original_pos[2])
	: glm::vec3(p.pos[0], p.pos[1], p.pos[2]);
      float d = glm::length(pos - center);
      maxDist = std::max(maxDist, d);
      minDist = std::min(minDist, d);

      float val = p.getValue(var);    
      maxvalue = std::max(maxvalue, val);
      minvalue = std::min(minvalue, val);
    }
    // 各粒子をビンに分けて平均値を算出
  
    if(plotXAxisLog == true)
      if(minDist < 1.e-6 * maxDist)
	minDist = 1.e-6 * maxDist;

    xmax = maxDist;
    xmin = minDist;
    ymax = maxvalue;
    ymin = minvalue;
  }
  
  double dr = (xmax - xmin)/bins;
  double dln_r= log(xmax / xmin)/bins;  

  for (const ParticleData &p : originalParticles) {
    if(p.type != 0)
      continue;
    
    glm::vec3 pos = useOriginal ? glm::vec3(p.original_pos[0], p.original_pos[1], p.original_pos[2])
      : glm::vec3(p.pos[0], p.pos[1], p.pos[2]);
    float d = glm::length(pos - center);
    
    int bin;
    if(plotXAxisLog == false)
      bin = int(d/dr);
    else
      bin = int(log(d / xmin) / dln_r);
    
    if (bin >= bins) bin = bins - 1;
    if (bin < 0) bin = 0;

    float val = p.getValue(var);    
    profile[bin] += val * p.mass;
    masses[bin] += p.mass;
    counts[bin]++;
  }
  
  
  for (int i = 0; i < bins; i++) {
    if (counts[i] > 0) profile[i] /= masses[i];
    if(plotXAxisLog == false)    
      x_coord[i] = dr * i;
    else
      x_coord[i] = xmin * exp(dln_r * i);      
  }
  
  return std::make_tuple(std::move(x_coord), std::move(profile));
}



void radialProfile::ShowRadialProfileUI(TrackingVector<ParticleData>& part) {
  if (!showWindowRadialProfile) return;
  
  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);  
  ImGui::Begin("Radial Profile", &showWindowRadialProfile, ImGuiWindowFlags_None);
  
  const char* quantities[] = { "Density", "Temperature", "val", "val2", "Hsml" };
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
  
  // 手動レンジ入力用（autoRange==false の場合）
  if (!autoRange)
    {
      ImGui::InputFloat("X Axis Min", &xmin, 0.0f, 0.0f, "%g");
      ImGui::InputFloat("X Axis Max", &xmax, 0.0f, 0.0f, "%g");
      ImGui::InputFloat("Y Axis Min", &ymin, 0.0f, 0.0f, "%g");
      ImGui::InputFloat("Y Axis Max", &ymax, 0.0f, 0.0f, "%g");
    }  

  if (ImGui::Button("Compute radial profile")){
    std::tie(radialProfileDataCoord, radialProfileDataValue)
      = computeRadialProfile(part, radialProfileUseOriginal, plotXAxisLog, radialProfileBins, var, autoRange, xmin, xmax, ymin, ymax);

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
