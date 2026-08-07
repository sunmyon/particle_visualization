#pragma once
#include <glm/glm.hpp>

struct ViewFilterConfig {
  bool enabled = false;  

  bool sphereEnabled = true;
  glm::vec3 center{0.0f};
  float radiusCullingSphere = 1.0f;

  bool sliceEnabled = false;
  glm::vec3 slicePoint{0.0f};
  int sliceAxis = 2;       // 0: X, 1: Y, 2: Z
  int sliceDirection = 1;  // +1: hide + side, -1: hide - side
};
