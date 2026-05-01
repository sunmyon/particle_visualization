#pragma once
#include <glm/glm.hpp>

struct ViewFilterConfig {
  bool enabled = false;  

  glm::vec3 center{0.0f};
  float radiusCullingSphere = 1.0f;
};
