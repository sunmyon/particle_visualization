#pragma once

#include <array>
#include "core/quantity.h"

constexpr int kNumParticleTypes = 6;

struct ParticleTypeVisualConfig {
  QuantityId selectedQuantity = QuantityId::Density;

  float pointSize = 1.0f;
  float colorMin = 0.0f;
  float colorMax = 1.0f;
  bool useLogScale = false;
  bool hideParticles = false;
  bool periodicColorBar = false;
  int colormapIndex = 0;
};

struct ParticleVisualConfig {
  std::array<ParticleTypeVisualConfig, kNumParticleTypes> types;
};
