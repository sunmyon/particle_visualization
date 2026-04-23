#pragma once
#include <glm/glm.hpp>
#include "core/tracking_vector.h"

class ParticleData;

struct ProjectionAngularMomentumFrame {
  glm::vec3 center{0.0f};
  glm::vec3 axis{0.0f, 0.0f, 1.0f};
  bool valid = false;
};

ProjectionAngularMomentumFrame ComputeAngularMomentumFrame(
    const TrackingVector<ParticleData>& particles,
    const glm::vec3& initialCenter,
    const float xlen[3]);

glm::quat UpdateTransformFromEuler(float *eulerAngles);
glm::quat BuildRotationFromZAxisTo(const glm::vec3& axis);
