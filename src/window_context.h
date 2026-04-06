#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

class WindowContext {
public:
  bool init(int width, int height, const char* title);
  void destroy();

  void attachCallbacks(GLFWcursorposfun mouseCb,
                       GLFWscrollfun scrollCb,
                       GLFWframebuffersizefun framebufferCb);

  void updateFramebufferSize(int width, int height);

  GLFWwindow* handle() const { return handle_; }

  int initialWidth() const { return initialWidth_; }
  int initialHeight() const { return initialHeight_; }

  int viewportX() const { return viewportX_; }
  int viewportY() const { return viewportY_; }
  int viewportWidth() const { return viewportWidth_; }
  int viewportHeight() const { return viewportHeight_; }

  glm::vec2 framebufferToImGui(float px, float py) const;
  glm::vec2 ndcToFramebuffer(const glm::vec3& ndc) const;
  glm::vec2 ndcToImGui(const glm::vec3& ndc) const;

  float projectionAspect() const;
  
private:
  GLFWwindow* handle_ = nullptr;

  int initialWidth_ = 1280;
  int initialHeight_ = 720;

  int viewportX_ = 0;
  int viewportY_ = 0;
  int viewportWidth_ = 1280;
  int viewportHeight_ = 720;
};
