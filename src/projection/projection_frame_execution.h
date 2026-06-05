#pragma once

#include "core/units.h"
#include "image/rgb_image.h"
#include "projection/projection_map_params.h"

#include <string>

class SimulationDataset;
class ProjectionMapGenerator;
struct QuantityState;

struct ProjectionFrameExecutionContext {
  SimulationDataset& particles;
  ProjectionMapGenerator& generator;
  const UnitSystem& units;
  const QuantityState& quantity;
};

struct ProjectionFrameOutputOptions {
  std::string path;
  bool writeFile = true;
  bool keepImage = true;
};

struct ProjectionFrameResult {
  bool ok = false;
  std::string error;
  std::string warning;
  std::string outputPath;
  RgbImage image;
};

std::string ResolveProjectionMapOutputPath(const ProjectionMapParams& params,
                                           int currentFileIndex,
                                           std::string* warning = nullptr);

ProjectionFrameResult ExecuteProjectionFrame(ProjectionFrameExecutionContext& projection,
                                             ProjectionMapParams params,
                                             double time,
                                             ProjectionFrameOutputOptions output);
