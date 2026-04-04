#pragma once

#include <glm/glm.hpp>
#include <functional>

#include "data/particle_block.h"
#include "core/tracking_vector.h"
#include "core/quantity.h"
#include "convex_hull_interface.h"

#ifdef USE_CONVEX_HULL
#include <memory>
#endif

struct Histogram2DParams {
  QuantityId var1 = QuantityId::Density;
  QuantityId var2 = QuantityId::Temperature;

  int bins1 = 50;
  int bins2 = 50;

  bool autoRange = true;
  float range1_min = 0.0f;
  float range1_max = 1.0f;
  float range2_min = 0.0f;
  float range2_max = 1.0f;

  bool logScaleX = true;
  bool logScaleY = true;
  bool logScaleColor = true;

  bool useCameraCenter = false;
  float cameraRadius = 10.0f;

#ifdef USE_CONVEX_HULL
  bool useConvexHull = false;
#endif
};

struct Histogram2DResult {
  TrackingVector<float> centers1;
  TrackingVector<float> centers2;
  TrackingVector<TrackingVector<float>> values;

  float range1_min = 0.0f;
  float range1_max = 1.0f;
  float range2_min = 0.0f;
  float range2_max = 1.0f;

  bool valid = false;
};

class Histogram2DComputer {
public:
  explicit Histogram2DComputer(const glm::vec3& center)
    : camCenter(center) {}

#ifdef USE_CONVEX_HULL
  void setConvexHulls(const TrackingVector<std::shared_ptr<IConvexHull>>& convexHulls) {
    convexHullCache = convexHulls;
  }
#endif

  Histogram2DResult compute(const ParticleBlock& partblock,
                            const Histogram2DParams& params);

private:
  const glm::vec3& camCenter;

#ifdef USE_CONVEX_HULL
  TrackingVector<std::shared_ptr<IConvexHull>> convexHullCache;
#endif
};
