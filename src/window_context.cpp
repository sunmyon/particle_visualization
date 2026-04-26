#include <glad/glad.h>
#include "window_context.h"

#include <imgui.h>
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
                                    GLFWkeyfun keyCb,
                                    GLFWframebuffersizefun framebufferCb)
{
  if (!handle_) return;

  glfwSetCursorPosCallback(handle_, mouseCb);
  glfwSetScrollCallback(handle_, scrollCb);
  glfwSetKeyCallback(handle_, keyCb);
  glfwSetFramebufferSizeCallback(handle_, framebufferCb);
}

void WindowContext::requestClose()
{
  if (handle_) {
    glfwSetWindowShouldClose(handle_, true);
  }
}

void WindowContext::present()
{
  if (handle_) {
    glfwSwapBuffers(handle_);
  }
}

void WindowContext::updateFramebufferSize(int width, int height)
{
  framebufferWidth_ = width;
  framebufferHeight_ = height;

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

glm::vec2 WindowContext::framebufferToImGui(float px, float py) const
{
  ImGuiIO& io = ImGui::GetIO();

  const float scaleX = io.DisplayFramebufferScale.x;
  const float scaleY = io.DisplayFramebufferScale.y;
  const float fbH    = io.DisplaySize.y * scaleY;

  return glm::vec2(px / scaleX,
                   (fbH - py) / scaleY);
}

glm::vec2 WindowContext::ndcToFramebuffer(const glm::vec3& ndc) const
{
  const float px = viewportX_ +
                   (ndc.x * 0.5f + 0.5f) * static_cast<float>(viewportWidth_);
  const float py = viewportY_ +
                   (ndc.y * 0.5f + 0.5f) * static_cast<float>(viewportHeight_);

  return glm::vec2(px, py);
}

glm::vec2 WindowContext::ndcToImGui(const glm::vec3& ndc) const
{
  glm::vec2 fb = ndcToFramebuffer(ndc);
  return framebufferToImGui(fb.x, fb.y);
}

float WindowContext::projectionAspect() const
{
  if (viewportHeight_ <= 0)
    return 1.0f;

  return static_cast<float>(viewportWidth_) /
         static_cast<float>(viewportHeight_);
}
