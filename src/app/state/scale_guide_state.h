#pragma once

#include <vector>

#include "render/scale_guide_label_overlay.h"
#include "render/scene_objects.h"

enum class ScaleGuideShapeType : int {
  Circle = 0,
  Square = 1,
  Box = 2
};

struct ScaleGuideObjectConfig {
  bool enabled = true;
  ScaleGuideShapeType type = ScaleGuideShapeType::Circle;
  float center[3] = {0.0f, 0.0f, 0.0f};
  int plane = 0; // 0=XY, 1=XZ, 2=YZ. Used by circle and square.

  int circleMode = 0; // 0=powers of 10, 1=fixed radius.
  float circleMinRadius = 10.0f;
  float circleMaxRadius = 10000.0f;
  float circleFixedRadius = 100.0f;

  float squareSize = 100.0f;
  float boxSize[3] = {100.0f, 100.0f, 100.0f};
};

struct ScaleGuideConfig {
  int addShapeType = static_cast<int>(ScaleGuideShapeType::Circle);
  std::vector<ScaleGuideObjectConfig> objects;
  bool dirty = true;
};

struct ScaleGuideDerivedState {
  bool cpuUpdated = false;
  float renderedWorldToRenderScale = -1.0f;
  std::vector<LineObject> lines;
  std::vector<ScaleGuideLabelItem> labels;
};
