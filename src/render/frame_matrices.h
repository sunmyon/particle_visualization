#pragma once

#include <glm/glm.hpp>

struct CameraContext;
struct RenderViewport;

struct FrameMatrices {
  glm::mat4 model{1.0f};
  glm::mat4 view{1.0f};
  glm::mat4 projection{1.0f};
  glm::mat4 invView{1.0f};
  glm::mat4 invProj{1.0f};
  glm::vec3 camForward{0.0f, 0.0f, -1.0f};
  float focalPx = 1.0f;
  float nearClip = 0.1f;
  float farClip = 1000.0f;
  int viewportW = 1;
  int viewportH = 1;
};

FrameMatrices BuildFrameMatrices(const CameraContext& camCtx,
                                 const RenderViewport& viewport);
