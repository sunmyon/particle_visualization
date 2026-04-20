#pragma once

#include <tuple>
#include <string>
#include <glm/glm.hpp>

#include "data/particle_block.h"
#include "core/tracking_vector.h"
#include "core/quantity.h"
#include "core/units.h"

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

struct NormalizationContext;
class RadialProfileComputer {
public:
  RadialProfileComputer() = default;
  void setUnits(UnitSystem& units) {
    units_ = units;
  }

  RadialProfileResult compute(const ParticleBlock& partblock,
			      const NormalizationContext& normalization,
                              const RadialProfileParams& params,
			      const glm::vec3& cam_center);

private:
  glm::vec3 v_center = glm::vec3(0);
  UnitSystem units_;
};


