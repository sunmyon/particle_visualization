#pragma once

#include <glm/vec3.hpp>

#include "data/particle_block.h"
#include "core/tracking_vector.h"
#include "core/quantity.h"
#include "geometry/convex_hull_interface.h"

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

struct Histogram2DContext {
  const glm::vec3* cameraCenter = nullptr;

#ifdef USE_CONVEX_HULL
  const TrackingVector<std::shared_ptr<IConvexHull>>* convexHulls = nullptr;
#endif
};

class Histogram2DComputer {
public:
  Histogram2DResult compute(const ParticleBlock& partblock,
                            const Histogram2DParams& params,
                            const Histogram2DContext& ctx) const;
};

