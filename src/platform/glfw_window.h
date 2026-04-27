#pragma once

#include "platform/window_backend.h"

#ifndef PARTICLE_VIS_HEADLESS_ONLY
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

class GlfwWindow final : public WindowBackend {
public:
  bool initLibrary() override;
  bool createWindow(int width, int height, const char* title) override;
  void destroy() override;

  void* nativeHandle() const override;

  void pollEvents() override;
  void requestClose() override;
  bool shouldClose(bool closeRequested) const override;
  double timeSeconds() const override;
  void framebufferSize(int& width, int& height) const override;

private:
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  GLFWwindow* handle_ = nullptr;
#endif
  bool initialized_ = false;
};
