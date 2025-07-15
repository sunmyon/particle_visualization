#pragma once

class radialProfile{
private:
  bool radialProfileComputed = false;
  bool showWindowRadialProfile = false;
  TrackingVector<float> radialProfileDataCoord, radialProfileDataValue;
  
  // 軸スケール変更用の変数（ImPlot 用）
  bool plotXAxisLog = true;
  bool plotYAxisLog = true;
  
  // Radial profile のビン数、original 座標を使うかどうかの選択
  int radialProfileBins = 50;
  bool radialProfileUseOriginal = false;

  bool autoRange = true;
  float xmin = 0.0f;
  float xmax = 1.0f;
  float ymin = 0.0f;
  float ymax = 1.0f;

  const glm::vec3 camCenter; // ここではカメラの注視点を中心とする例
  
  std::tuple<TrackingVector<float>, TrackingVector<float>>
  computeRadialProfile(TrackingVector<ParticleData>& originalParticles,
		       bool useOriginal, bool useLog, int bins, const std::string &var,
		       bool autorange, float& xmin, float& xmax, float& ymin, float& ymax);

public:
  radialProfile(const glm::vec3& center):
    camCenter(center)
  {}
  
  void ShowRadialProfileUI(TrackingVector<ParticleData>& originalParticles);
  void showWindow(){
    showWindowRadialProfile = true;
  };
};
