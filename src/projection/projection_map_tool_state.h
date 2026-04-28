#pragma once
#include "projection/projection_map_params.h"
#include "render/scene_objects.h"

struct ProjectionMapToolState {
  ProjectionMapParams params;
  CuboidObject interactiveCuboid;
  int appliedSelectedAxis = -1;
  uint64_t revision = 0;
};

struct ProjectionMapRequestState {
  bool paramsChanged = false;
  ProjectionMapParams params;
  bool renderRequested = false;
  bool moveCenterToCameraRequested = false;
  bool setAxisFromAngularMomentumRequested = false;
  bool arcballDragRequested = false;
  float dragOldX = 0.0f;
  float dragOldY = 0.0f;
  float dragNewX = 0.0f;
  float dragNewY = 0.0f;
  float displayWidth = 1.0f;
  float displayHeight = 1.0f;
};
