#pragma once

#include <glm/glm.hpp>

struct BoundingBox {
  glm::vec3 min;
  glm::vec3 max;

  bool contains(const glm::vec3& p) const {
    return p.x >= min.x && p.x <= max.x &&
           p.y >= min.y && p.y <= max.y &&
           p.z >= min.z && p.z <= max.z;
  }
};

struct SpatialTreePoint {
  glm::vec3 pos;
  float val = 0.0f;
};

using ParticleDataForTree = SpatialTreePoint;
