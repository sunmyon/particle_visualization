#pragma once

#include <tuple>
#include <string>
#include <glm/glm.hpp>

#include "main.h"        // TrackingVector, ParticleData, ParticleBlock がここにある前提
#include "quantity.h"  // QuantityId, QuantityLabel, getScalarValue(...) がある前提

// --- X axis mode for 1D profile ---
enum class XAxisMode : int { Radius = 0, PosX = 1, PosY = 2, PosZ = 3, EnclosedMass = 4 };

inline const char* XAxisLabel(XAxisMode m) {
  switch (m) {
    case XAxisMode::Radius:       return "r";
    case XAxisMode::PosX:         return "x";
    case XAxisMode::PosY:         return "y";
    case XAxisMode::PosZ:         return "z";
    case XAxisMode::EnclosedMass: return "M(<r)";
  }
  return "Unknown";
}

class radialProfile {
private:
  bool radialProfileComputed = false;
  bool showWindowRadialProfile = false;

  TrackingVector<float> radialProfileDataCoord, radialProfileDataValue;

  // ImPlot axis scale
  bool plotXAxisLog = true;
  bool plotYAxisLog = true;

  // bins / original coords
  int  radialProfileBins = 50;
  bool radialProfileUseOriginal = false;
  bool flagAbsolute = false;

  bool autoRange = true;
  float xmin = 0.0f;
  float xmax = 1.0f;
  float ymin = 0.0f;
  float ymax = 1.0f;
  float rmax = 0.f;

  // --- UI state for X axis & Y quantity ---
  int selectedXAxis = 0;      // XAxisMode as int (0..4)
  int selectedVarIdx = 0;     // "virtual list index" = uiQ + optional mdot

  // camera center (normalized) reference
  const glm::vec3& camCenter;

  // velocity center used for v_rad/mdot
  glm::vec3 v_center = glm::vec3(0);

  // --- Units ---
  double UnitVelocity_in_cgs = 1.e5; // km/s
  double UnitLength_in_cm    = 3.08e18; // pc
  double UnitLength_in_pc    = 1.; // pc
  double UnitMass_in_g       = 1.98e33; // Msun
  double UnitMass_in_msolar  = 1.; // Msun
  double UnitTime_in_s       = 3.08e13;
  double UnitTime_in_yr      = 0.97e6;

  static constexpr double yr_in_sec     = 3.15576e7;
  static constexpr double msolar_in_g   = 1.989e33;
  static constexpr double au_in_cm      = 1.49598e13;
  static constexpr double pc_in_cm      = 3.085678e18;
  static constexpr double kpc_in_cm     = 3.085678e21;
  static constexpr double Mpc_in_cm     = 3.085678e24;
  static constexpr double GravConst     = 6.6743e-8;

  // ============================================================
  // Core compute (recommended): uses ParticleBlock so SoA fields are available
  // isMDot==true  -> mdot (profile-only derived)
  // isMDot==false -> base quantity var1 (QuantityId)
  // ============================================================
  std::tuple<TrackingVector<float>, TrackingVector<float>>
  computeRadialProfile(const ParticleBlock& partblock,
                       bool useOriginal, int bins,
                       XAxisMode xmode,
                       bool isMDot, QuantityId var1,
                       bool autorange,
                       float& xmin, float& xmax, float& ymin, float& ymax, float& rmax);

public:
  radialProfile(const glm::vec3& center)
    : camCenter(center)
  {}

  // ============================================================
  // UI (recommended): pass ParticleBlock (has uiQ/nUIQ + SoA)
  // ============================================================
  void ShowRadialProfileUI(const ParticleBlock& partblock,
                           double unitmass_in_g, double unitlength_in_cm, double unittime_in_s);

  void showWindow() { showWindowRadialProfile = true; }

  void setUnits(double unitmass_in_g, double unitlength_in_cm, double unittime_in_s) {
    UnitMass_in_g    = unitmass_in_g;
    UnitLength_in_cm = unitlength_in_cm;
    UnitTime_in_s    = unittime_in_s;

    UnitLength_in_pc   = UnitLength_in_cm / pc_in_cm;
    UnitMass_in_msolar = UnitMass_in_g / msolar_in_g;
    UnitTime_in_s      = UnitLength_in_cm / UnitVelocity_in_cgs;
    UnitTime_in_yr     = UnitTime_in_s / yr_in_sec;
  }
};
