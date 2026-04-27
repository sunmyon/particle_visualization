#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

struct ProjectionMapContext {
  char selectedType = 0;

  glm::vec3 center{0.0f};
  glm::quat cuboidTransform{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 planeNormal{0.0f, 0.0f, 1.0f};

  const float* colorMap = nullptr;
  int colorMapSize = 0;

  double scaleToPhysical = 1.0;
  double time = 0.0;
};

glm::quat BuildProjectionTransformFromEuler(const float* eulerAngles);
