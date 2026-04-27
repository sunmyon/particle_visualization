#include "platform/glfw_window.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
using Clock = std::chrono::steady_clock;

void GlfwErrorCallback(int error, const char* description)
{
  std::cerr << "GLFW error " << error << ": "
            << (description ? description : "(no description)") << std::endl;
}

const char* EnvOrUnset(const char* name)
{
  const char* value = std::getenv(name);
  return (value && value[0] != '\0') ? value : "(unset)";
}

Clock::time_point& StartTime()
{
  static Clock::time_point t0 = Clock::now();
  return t0;
}
}

bool GlfwWindow::initLibrary()
{
#ifdef PARTICLE_VIS_HEADLESS_ONLY
  return false;
#else
  glfwSetErrorCallback(GlfwErrorCallback);

#if defined(__linux__) && defined(GLFW_PLATFORM) && defined(GLFW_PLATFORM_X11)
  // In SSH X forwarding sessions DISPLAY is set, but some systems still let
  // GLFW probe Wayland first and fail on missing XDG_RUNTIME_DIR.
  if (std::getenv("DISPLAY") && !std::getenv("PARTICLE_VIS_GLFW_PLATFORM")) {
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  }
#endif

  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    std::cerr << "DISPLAY=" << EnvOrUnset("DISPLAY")
              << " WAYLAND_DISPLAY=" << EnvOrUnset("WAYLAND_DISPLAY")
              << " XDG_RUNTIME_DIR=" << EnvOrUnset("XDG_RUNTIME_DIR")
              << std::endl;
    return false;
  }
  initialized_ = true;
  return true;
#endif
}

bool GlfwWindow::createWindow(int width, int height, const char* title)
{
#ifdef PARTICLE_VIS_HEADLESS_ONLY
  (void)width;
  (void)height;
  (void)title;
  return false;
#else
  if (!initialized_) {
    return false;
  }

  handle_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
  if (!handle_) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    std::cerr << "DISPLAY=" << EnvOrUnset("DISPLAY")
              << " WAYLAND_DISPLAY=" << EnvOrUnset("WAYLAND_DISPLAY")
              << " XDG_RUNTIME_DIR=" << EnvOrUnset("XDG_RUNTIME_DIR")
              << std::endl;
    glfwTerminate();
    initialized_ = false;
    return false;
  }

  return true;
#endif
}

void GlfwWindow::destroy()
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (handle_) {
    glfwDestroyWindow(handle_);
    handle_ = nullptr;
  }
  if (initialized_) {
    glfwTerminate();
    initialized_ = false;
  }
#endif
}

void* GlfwWindow::nativeHandle() const
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  return handle_;
#else
  return nullptr;
#endif
}

void GlfwWindow::pollEvents()
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (handle_) {
    glfwPollEvents();
  }
#endif
}

std::unique_ptr<WindowBackend> CreateDefaultWindowBackend()
{
  return std::make_unique<GlfwWindow>();
}

void GlfwWindow::requestClose()
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (handle_) {
    glfwSetWindowShouldClose(handle_, true);
  }
#endif
}

bool GlfwWindow::shouldClose(bool closeRequested) const
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (handle_) {
    return glfwWindowShouldClose(handle_);
  }
#endif
  return closeRequested;
}

double GlfwWindow::timeSeconds() const
{
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (initialized_) {
    return glfwGetTime();
  }
#endif

  const auto now = Clock::now();
  return std::chrono::duration<double>(now - StartTime()).count();
}

void GlfwWindow::framebufferSize(int& width, int& height) const
{
  width = 0;
  height = 0;
#ifndef PARTICLE_VIS_HEADLESS_ONLY
  if (handle_) {
    glfwGetFramebufferSize(handle_, &width, &height);
  }
#endif
}
