#pragma once

#include <glm/vec3.hpp>
#include <memory>
#include <vector>

#include "core/tracking_vector.h"
#include "analysis/convex_hull/convex_hull_interface.h"

class ConvexHullGenerator {
public:
  ConvexHullGenerator();
  ~ConvexHullGenerator();

  std::shared_ptr<IConvexHull>
  buildHull(const std::vector<glm::vec3>& points);

  TrackingVector<float>
  buildLineVertices(const std::vector<glm::vec3>& points);

private:
  ConvexHullGenerator(const ConvexHullGenerator&) = delete;
  ConvexHullGenerator& operator=(const ConvexHullGenerator&) = delete;

  struct Impl;
  Impl* impl = nullptr;
};
