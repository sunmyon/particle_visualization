#pragma once
#include <glm/glm.hpp>
struct ParticleDataForKdTree;  // Include the definition or forward-declare it.

/// Density-estimator implementations should implement this interface.
struct IDensityEstimator {
  virtual ~IDensityEstimator() = default;
  /// pos: sampling position.
  /// return: scalar value at that position, such as density.
  virtual float sample(const glm::vec3& pos) const = 0;
};
