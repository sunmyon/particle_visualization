
#include <vector>
#include <glm/gtc/quaternion.hpp>

#include <vector>
#include "projection/projection_geometry.h"
#include "data/sample_coordinates.h"
#include "data/simulation_element.h"

glm::quat UpdateTransformFromEuler(float *eulerAngles)
{
  glm::quat qx = glm::angleAxis(glm::radians(eulerAngles[0]), glm::vec3(1.0f, 0.0f, 0.0f));
  glm::quat qy = glm::angleAxis(glm::radians(eulerAngles[1]), glm::vec3(0.0f, 1.0f, 0.0f));
  glm::quat qz = glm::angleAxis(glm::radians(eulerAngles[2]), glm::vec3(0.0f, 0.0f, 1.0f));
    
  return qz * qy * qx;  
}  

ProjectionAngularMomentumFrame ComputeAngularMomentumFrame(
    const std::vector<SimulationElement>& particles,
    float worldToRenderScale,
    const glm::vec3& initialCenter,
    const float xlen[3])
{
  ProjectionAngularMomentumFrame result;
  result.center = initialCenter;

  float xmin[3];
  float xmax[3];
  for (int k = 0; k < 3; ++k) {
    xmin[k] = -0.5f * xlen[k];
    xmax[k] =  0.5f * xlen[k];
  }

  double weightedPos[3] = {0.0, 0.0, 0.0};
  double weightedVel[3] = {0.0, 0.0, 0.0};
  double totalMass = 0.0;
  double totalWeight = 0.0;

  const double lenCrit2 =
    4.0 * (xlen[0] * xlen[0] +
           xlen[1] * xlen[1] +
           xlen[2] * xlen[2]);

  for (const auto& p : particles) {
    const glm::vec3 pos = renderPosition(p, worldToRenderScale);
    const glm::vec3 localPos = pos - initialCenter;

    const double r2 =
      static_cast<double>(localPos.x) * localPos.x +
      static_cast<double>(localPos.y) * localPos.y +
      static_cast<double>(localPos.z) * localPos.z;

    if (r2 >= lenCrit2) {
      continue;
    }

    const double mass = p.mass;
    const double weight = p.density;

    weightedPos[0] += pos.x * weight;
    weightedPos[1] += pos.y * weight;
    weightedPos[2] += pos.z * weight;

    weightedVel[0] += mass * p.vel[0];
    weightedVel[1] += mass * p.vel[1];
    weightedVel[2] += mass * p.vel[2];

    totalMass += mass;
    totalWeight += weight;
  }

  if (totalMass <= 0.0 || totalWeight <= 0.0) {
    return result;
  }

  const glm::vec3 center(
    static_cast<float>(weightedPos[0] / totalWeight),
    static_cast<float>(weightedPos[1] / totalWeight),
    static_cast<float>(weightedPos[2] / totalWeight));

  const glm::vec3 meanVel(
    static_cast<float>(weightedVel[0] / totalMass),
    static_cast<float>(weightedVel[1] / totalMass),
    static_cast<float>(weightedVel[2] / totalMass));

  glm::dvec3 angularMomentum(0.0);
  double angularMomentumMass = 0.0;

  for (const auto& p : particles) {
    const glm::vec3 localPos =
      renderPosition(p, worldToRenderScale) - center;

    if (localPos.x < xmin[0] || localPos.x > xmax[0] ||
        localPos.y < xmin[1] || localPos.y > xmax[1] ||
        localPos.z < xmin[2] || localPos.z > xmax[2]) {
      continue;
    }

    const double mass = p.mass;
    const glm::dvec3 r(localPos.x, localPos.y, localPos.z);
    const glm::dvec3 dv(p.vel[0] - meanVel.x,
                        p.vel[1] - meanVel.y,
                        p.vel[2] - meanVel.z);

    angularMomentum += mass * glm::cross(r, dv);
    angularMomentumMass += mass;
  }

  if (angularMomentumMass <= 0.0) {
    return result;
  }

  angularMomentum /= angularMomentumMass;

  const double norm2 = glm::dot(angularMomentum, angularMomentum);
  if (norm2 <= 0.0 || !std::isfinite(norm2)) {
    return result;
  }

  result.center = center;
  result.axis = glm::normalize(glm::vec3(angularMomentum));
  result.valid = true;
  return result;
}


glm::quat BuildRotationFromZAxisTo(const glm::vec3& axis)
{
  const glm::vec3 normal(0.0f, 0.0f, 1.0f);
  const glm::vec3 target = glm::normalize(axis);

  const float dot = glm::dot(normal, target);

  if (dot > 0.9999f) {
    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  }

  if (dot < -0.9999f) {
    return glm::angleAxis(glm::pi<float>(), glm::vec3(1.0f, 0.0f, 0.0f));
  }

  const glm::vec3 v = glm::cross(normal, target);
  const float w = 1.0f + dot;
  return glm::normalize(glm::quat(w, v.x, v.y, v.z));
}
