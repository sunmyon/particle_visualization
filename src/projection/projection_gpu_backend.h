#pragma once

#include <vector>

#include <glm/glm.hpp>

struct ProjectionGpuParticle {
  float pos[3] = {0.0f, 0.0f, 0.0f};
  float val = 0.0f;
  float colorVal = 0.0f;
  float density = 0.0f;
  float mass = 0.0f;
  float hsml = 0.0f;
};

inline constexpr int kProjectionGpuMaxTfComponents = 16;

struct ProjectionGpuTransferComponent {
  int type = 0; // 0=Gaussian, 1=Box, 2=Triangle.
  float center = 1.0f;
  float width = 1.0f;
  float amplitude = 0.0f;
  bool logDomain = true;
};

struct ProjectionGpuMapInput {
  int width = 0;
  int height = 0;
  int depth = 0;
  float dx = 0.0f;
  float dy = 0.0f;
  float dz = 0.0f;
  float xminLocal[3] = {0.0f, 0.0f, 0.0f};
  glm::vec3 center{0.0f};
  glm::vec3 uAxis{1.0f, 0.0f, 0.0f};
  glm::vec3 vAxis{0.0f, 1.0f, 0.0f};
  bool densityWeight = false;
  float valueMin = 0.0f;
  float valueMax = 1.0f;
  float colorValueMin = 0.0f;
  float colorValueMax = 1.0f;
  bool colorLogScale = true;
  float renderColor[3] = {0.35f, 1.0f, 0.8f};
  std::vector<float> colorMap;
  int colorMapSize = 0;
  std::vector<ProjectionGpuTransferComponent> transferComponents;
  std::vector<ProjectionGpuParticle> particles;
};

struct ProjectionGpuMapOutput {
  std::vector<float> values;
  std::vector<float> weights;
  std::vector<float> rgb;
  double elapsedMs = 0.0;
};

struct ProjectionGpuLabelGrid {
  int width = 0;
  int height = 0;
  int depth = 0;
  std::vector<int> labels;
  double elapsedMs = 0.0;
};
