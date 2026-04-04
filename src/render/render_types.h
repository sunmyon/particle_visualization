#pragma once
#include <glm/glm.hpp>

struct InstancedSolidItem {
  glm::mat4 model{1.0f};
  glm::vec3 color{1.0f};
  float opacity = 1.0f;
};
