#pragma once

class OpenGLContext {
public:
  void configureGlfwWindowHints() const;
  bool initFromGlfwWindow(void* glfwWindow);
  bool initHeadless(int width, int height);
  void destroy();
  void present(void* glfwWindow);

  bool isHeadless() const { return headless_; }

private:
  bool headless_ = false;

#ifdef PARTICLE_VIS_HAVE_EGL
  void* eglDisplay_ = nullptr;
  void* eglSurface_ = nullptr;
  void* eglContext_ = nullptr;
#endif
};
