#pragma once

#include <glm/vec3.hpp>

#include "data/simulation_block.h"
#include <vector>
#include "core/quantity.h"
#include "analysis/convex_hull/convex_hull_interface.h"

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
  int particleType = 0;
  bool showScatter = false;
  int scatterMaxPoints = 10000;

  bool useCameraCenter = false;
  float cameraRadius = 10.0f;

#ifdef USE_CONVEX_HULL
  bool useConvexHull = false;
#endif
};

struct Histogram2DResult {
  std::vector<float> centers1;
  std::vector<float> centers2;
  std::vector<std::vector<float>> values;

  float range1_min = 0.0f;
  float range1_max = 1.0f;
  float range2_min = 0.0f;
  float range2_max = 1.0f;

  std::vector<float> scatterX;
  std::vector<float> scatterY;

  bool valid = false;
};

struct Histogram2DContext {
  const glm::vec3* dataCenter = nullptr;
  float dataRadius = 0.0f;

#ifdef USE_CONVEX_HULL
  const std::vector<std::shared_ptr<IConvexHull>>* convexHulls = nullptr;
#endif
};

class Histogram2DComputer {
public:
  Histogram2DResult compute(const SimulationBlock& partblock,
                            const Histogram2DParams& params,
                            const Histogram2DContext& ctx) const;
};
