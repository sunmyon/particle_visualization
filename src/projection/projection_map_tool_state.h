#pragma once
#include "projection/projection_map_params.h"
#include "object.h"

struct ProjectionMapToolState {
  ProjectionMapParams params;
  CuboidObject interactiveCuboid;
  bool renderRequested = false;
};
