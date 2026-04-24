#pragma once
#include <array>
#include <glm/glm.hpp>

struct ParticleSelectionOption {
  glm::vec3 center = glm::vec3(0.0f);
  float radius = -1.0f;
  std::array<bool, 6> useType = {true, false, false, true, true, true};
  bool flagSubtractBulkVelocity = true;
};
