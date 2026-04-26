#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

class WindowContext {
public:
  bool init(int width, int height, const char* title);
  bool initHeadless(int width, int height);
  void destroy();

  void attachCallbacks(GLFWcursorposfun mouseCb,
                       GLFWscrollfun scrollCb,
                       GLFWkeyfun keyCb,
                       GLFWframebuffersizefun framebufferCb);

  void updateFramebufferSize(int width, int height);
  void pollEvents();
  void requestClose();
  void present();
  bool shouldClose() const;
  double timeSeconds() const;

  GLFWwindow* handle() const { return handle_; }
  bool isHeadless() const { return headless_; }
  GLenum readBufferMode() const { return headless_ ? GL_FRONT : GL_BACK; }

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
  GLFWwindow* handle_ = nullptr;
  bool glfwInitialized_ = false;
  bool headless_ = false;
  bool closeRequested_ = false;

#ifdef PARTICLE_VIS_HAVE_EGL
  void* eglDisplay_ = nullptr;
  void* eglSurface_ = nullptr;
  void* eglContext_ = nullptr;
#endif

  int initialWidth_ = 1280;
  int initialHeight_ = 720;

  int viewportX_ = 0;
  int viewportY_ = 0;
  int viewportWidth_ = 1280;
  int viewportHeight_ = 720;
  int framebufferWidth_ = 1280;
  int framebufferHeight_ = 720;
};
