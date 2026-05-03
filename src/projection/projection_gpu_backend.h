#pragma once

#include <vector>

#include <glm/glm.hpp>

struct ProjectionGpuParticle {
  float pos[3] = {0.0f, 0.0f, 0.0f};
  float val = 0.0f;
  float density = 0.0f;
  float mass = 0.0f;
  float hsml = 0.0f;
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
  std::vector<ProjectionGpuParticle> particles;
};

struct ProjectionGpuMapOutput {
  std::vector<float> values;
  std::vector<float> weights;
  double elapsedMs = 0.0;
};

struct ProjectionGpuLabelGrid {
  int width = 0;
  int height = 0;
  int depth = 0;
  std::vector<int> labels;
  double elapsedMs = 0.0;
};
