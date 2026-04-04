#pragma once

#include <tuple>
#include <string>
#include <glm/glm.hpp>

#include "data/particle_block.h"
#include "core/tracking_vector.h"
#include "core/quantity.h"  // QuantityId, QuantityLabel, getScalarValue(...) がある前提

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

struct RadialProfileParams {
  bool useOriginal = false;
  int bins = 50;
  XAxisMode xmode = XAxisMode::Radius;
  bool isMDot = false;
  QuantityId var1 = QuantityId::Density;
  bool autorange = true;
  bool flagAbsolute = false;
  bool plotXAxisLog = true;
  bool plotYAxisLog = true;
  float xmin = 0.0f;
  float xmax = 1.0f;
  float ymin = 0.0f;
  float ymax = 1.0f;
  float rmax = 0.0f;
};

struct RadialProfileResult {
  TrackingVector<float> x;
  TrackingVector<float> y;
  float xmin = 0.0f;
  float xmax = 1.0f;
  float ymin = 0.0f;
  float ymax = 1.0f;
  bool valid = false;
};

class RadialProfileComputer {
public:
  explicit RadialProfileComputer(const glm::vec3& center)
    : camCenter(center) {}

  void setUnits(double unitmass_in_g, double unitlength_in_cm, double unittime_in_s) {
    UnitMass_in_g    = unitmass_in_g;
    UnitLength_in_cm = unitlength_in_cm;
    UnitTime_in_s    = unittime_in_s;

    UnitLength_in_pc   = UnitLength_in_cm / pc_in_cm;
    UnitMass_in_msolar = UnitMass_in_g / msolar_in_g;
    UnitTime_in_s      = UnitLength_in_cm / UnitVelocity_in_cm_per_s;
    UnitTime_in_yr     = UnitTime_in_s / yr_in_sec;
  }

  RadialProfileResult compute(const ParticleBlock& partblock,
                              const RadialProfileParams& params);

private:
  const glm::vec3& camCenter;
  glm::vec3 v_center = glm::vec3(0);

  double UnitVelocity_in_cm_per_s = 1.e5;
  double UnitLength_in_cm = 3.08e18;
  double UnitLength_in_pc = 1.;
  double UnitMass_in_g = 1.98e33;
  double UnitMass_in_msolar = 1.;
  double UnitTime_in_s = 3.08e13;
  double UnitTime_in_yr = 0.97e6;

  static constexpr double yr_in_sec   = 3.15576e7;
  static constexpr double msolar_in_g = 1.989e33;
  static constexpr double au_in_cm      = 1.49598e13;
  static constexpr double pc_in_cm      = 3.085678e18;
  static constexpr double kpc_in_cm     = 3.085678e21;
  static constexpr double Mpc_in_cm     = 3.085678e24;
  static constexpr double GravConst     = 6.6743e-8;
};


