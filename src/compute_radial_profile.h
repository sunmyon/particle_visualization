#pragma once
#include <glm/glm.hpp>

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
  bool flagAbsolute = false;
  
  bool autoRange = true;
  float xmin = 0.0f;
  float xmax = 1.0f;
  float ymin = 0.0f;
  float ymax = 1.0f;
  float rmax = 0.f;
  
  const glm::vec3 &camCenter; // ここではカメラの注視点を中心とする例
  glm::vec3 v_center; // ここではカメラの注視点を中心とする例
  
  std::tuple<TrackingVector<float>, TrackingVector<float>>
  computeRadialProfile(TrackingVector<ParticleData>& originalParticles,
		       bool useOriginal, bool useLog, int bins, const std::string &var,
		       bool autorange, float& xmin, float& xmax, float& ymin, float& ymax, float& rmax);

  double UnitVelocity_in_cgs = 1.e5; // km/s
  double UnitLength_in_cm = 3.08e18; //pc
  double UnitLength_in_pc = 1.; //pc
  double UnitMass_in_g = 1.98e33; //Msun
  double UnitMass_in_msolar = 1.; //Msun
  double UnitTime_in_s = 3.08e13;
  double UnitTime_in_yr = 0.97e6;

  static constexpr double yr_in_sec = 3.15576e7;
  static constexpr double msolar_in_g = 1.989e33;
  static constexpr double au_in_cm = 1.49598e13;
  static constexpr double pc_in_cm = 3.085678e18;
  static constexpr double kpc_in_cm = 3.085678e21;
  static constexpr double Mpc_in_cm = 3.085678e24;
  static constexpr double GravConst = 6.6743e-8;
  
public:
  radialProfile(const glm::vec3& center):
    camCenter(center)
  {}
  
  void ShowRadialProfileUI(TrackingVector<ParticleData>& part, double unitmass_in_g, double unitlength_in_cm, double unittime_in_s);
  void showWindow(){
    showWindowRadialProfile = true;
  };

  void setUnits(double unitmass_in_g, double unitlength_in_cm, double unittime_in_s){
    UnitMass_in_g = unitmass_in_g;
    UnitLength_in_cm = unitlength_in_cm;
    UnitTime_in_s = unittime_in_s;
    
    UnitLength_in_pc = UnitLength_in_cm / pc_in_cm;
    UnitMass_in_msolar = UnitMass_in_g / msolar_in_g;    
    UnitTime_in_s = UnitLength_in_cm / UnitVelocity_in_cgs;
    UnitTime_in_yr = UnitTime_in_s / yr_in_sec;
  }
};
