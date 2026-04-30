#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "camera.h"
#include "render/scene_objects.h"

class InteractionState {
public:
  void resetMouse() { firstMouse_ = true; }

  bool firstMouse() const { return firstMouse_; }

  void setMousePosition(float x, float y) {
    lastX_ = x;
    lastY_ = y;
    firstMouse_ = false;
  }

  float lastX() const { return lastX_; }
  float lastY() const { return lastY_; }

  float beginFrame(float currentFrame) {
    float delta = currentFrame - lastFrame_;
    lastFrame_ = currentFrame;
    return delta;
  }

  void markInputActivity(float currentFrame) {
    lastInputFrame_ = currentFrame;
  }

  bool inputActive(float currentFrame, float settleDelay) const {
    return lastInputFrame_ >= 0.0f &&
           currentFrame - lastInputFrame_ <= settleDelay;
  }

private:
  float lastX_ = 0.0f;
  float lastY_ = 0.0f;
  bool  firstMouse_ = true;
  float lastFrame_ = 0.0f;
  float lastInputFrame_ = -1.0f;
};

glm::vec3 MapToSphere(float x, float y,
                      float screenWidth,
                      float screenHeight);

void ApplyCameraPan(CameraContext& camCtx,
                    float xoffset,
                    float yoffset);

void ApplyCameraZoom(CameraContext& camCtx,
                     float yoffset,
                     float minZoom,
                     float maxZoom);

void ApplyCameraArcballRotation(CameraContext& camCtx,
                                float oldX, float oldY,
                                float newX, float newY,
                                float screenWidth,
                                float screenHeight);

glm::vec3 FlipLeftRight(const glm::vec3& v);

void UpdateCuboidTransformArcball(CuboidObject& cuboid,
                                  float oldX, float oldY,
                                  float newX, float newY,
                                  float screenWidth,
                                  float screenHeight,
                                  const glm::mat4& view,
                                  const glm::vec3& pivotWorld);
