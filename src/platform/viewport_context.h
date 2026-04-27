#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

class ViewportContext {
public:
  void setInitialSize(int width, int height);
  void updateFramebufferSize(int width, int height);

  int initialWidth() const { return initialWidth_; }
  int initialHeight() const { return initialHeight_; }

  int viewportX() const { return viewportX_; }
  int viewportY() const { return viewportY_; }
  int viewportWidth() const { return viewportWidth_; }
  int viewportHeight() const { return viewportHeight_; }
  int framebufferWidth() const { return framebufferWidth_; }
  int framebufferHeight() const { return framebufferHeight_; }

  glm::vec2 framebufferToImGui(float px, float py) const;
  glm::vec2 ndcToFramebuffer(const glm::vec3& ndc) const;
  glm::vec2 ndcToImGui(const glm::vec3& ndc) const;

  float projectionAspect() const;

private:
  int initialWidth_ = 1280;
  int initialHeight_ = 720;

  int viewportX_ = 0;
  int viewportY_ = 0;
  int viewportWidth_ = 1280;
  int viewportHeight_ = 720;
  int framebufferWidth_ = 1280;
  int framebufferHeight_ = 720;
};
