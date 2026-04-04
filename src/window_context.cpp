#include <glad/glad.h>
#include "window_context.h"

#include <iostream>
#include <cstdlib>

bool WindowContext::init(int width, int height, const char* title)
{
  initialWidth_ = width;
  initialHeight_ = height;

  if (!glfwInit()) {
    std::cerr << "Failed to initialize GLFW" << std::endl;
    return false;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  handle_ = glfwCreateWindow(initialWidth_, initialHeight_, title, nullptr, nullptr);
  if (!handle_) {
    std::cerr << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return false;
  }

  glfwMakeContextCurrent(handle_);

  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cerr << "Failed to initialize GLAD" << std::endl;
    glfwDestroyWindow(handle_);
    handle_ = nullptr;
    glfwTerminate();
    return false;
  }

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_PROGRAM_POINT_SIZE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  int fbW = 0, fbH = 0;
  glfwGetFramebufferSize(handle_, &fbW, &fbH);
  updateFramebufferSize(fbW, fbH);

  return true;
}

void WindowContext::destroy()
{
  if (handle_) {
    glfwDestroyWindow(handle_);
    handle_ = nullptr;
  }
  glfwTerminate();
}

void WindowContext::attachCallbacks(GLFWcursorposfun mouseCb,
                                    GLFWscrollfun scrollCb,
                                    GLFWframebuffersizefun framebufferCb)
{
  if (!handle_) return;

  glfwSetCursorPosCallback(handle_, mouseCb);
  glfwSetScrollCallback(handle_, scrollCb);
  glfwSetFramebufferSizeCallback(handle_, framebufferCb);
}

void WindowContext::updateFramebufferSize(int width, int height)
{
#ifdef USE_LETTERBOX
  const float targetAspect =
    static_cast<float>(initialWidth_) / static_cast<float>(initialHeight_);
  const float windowAspect =
    static_cast<float>(width) / static_cast<float>(height);

  if (windowAspect > targetAspect) {
    viewportHeight_ = height;
    viewportWidth_  = static_cast<int>(height * targetAspect);
    viewportX_      = (width - viewportWidth_) / 2;
    viewportY_      = 0;
  } else {
    viewportWidth_  = width;
    viewportHeight_ = static_cast<int>(width / targetAspect);
    viewportX_      = 0;
    viewportY_      = (height - viewportHeight_) / 2;
  }

  glViewport(viewportX_, viewportY_, viewportWidth_, viewportHeight_);
#else
  viewportX_ = 0;
  viewportY_ = 0;
  viewportWidth_ = width;
  viewportHeight_ = height;
  glViewport(0, 0, width, height);
#endif
}
