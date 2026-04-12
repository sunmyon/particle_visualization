#include "app_callbacks.h"
#include "app_state.h"
#include "window_context.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "interaction/interaction_utils.h"
#include "interaction/camera.h"


CallbackContext* GetCallbackContext(GLFWwindow* window)
{
  return static_cast<CallbackContext*>(glfwGetWindowUserPointer(window));
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
  CallbackContext* ctx = GetCallbackContext(window);
  if (!ctx || !ctx->window)
    return;

  ctx->window->updateFramebufferSize(width, height);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
  CallbackContext* ctx = GetCallbackContext(window);
  if (!ctx || !ctx->app)
    return;

  AppState& app = *ctx->app;

  if (ImGui::GetIO().WantCaptureMouse || app.view.camera.stopCameraMode)
    return;

  const bool leftPressed =
    (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);
  const bool shiftPressed =
    (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
     glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);

  if (!leftPressed) {
    app.runtime.interaction.resetMouse();
    return;
  }

  if (app.runtime.interaction.firstMouse()) {
    app.runtime.interaction.setMousePosition((float)xpos, (float)ypos);
    return;
  }

  const float oldX = app.runtime.interaction.lastX();
  const float oldY = app.runtime.interaction.lastY();

  const float xoffset = (float)xpos - oldX;
  const float yoffset = oldY - (float)ypos;

  app.runtime.interaction.setMousePosition((float)xpos, (float)ypos);

  if (shiftPressed) {
    ApplyCameraPan(app.view.camera, xoffset, yoffset);
    return;
  }

#if defined(ROTATE_ARCBALL)
  ImVec2 displaySize = ImGui::GetIO().DisplaySize;
  ApplyCameraArcballRotation(app.view.camera,
                             oldX, oldY,
                             (float)xpos, (float)ypos,
                             displaySize.x, displaySize.y);
#elif defined(ROTATE_QUATERNION)
  float sensitivity = 0.1f;

  float yawAngle = glm::radians(-xoffset * sensitivity);
  glm::quat qYaw = glm::angleAxis(yawAngle, glm::vec3(0.0f, 1.0f, 0.0f));

  glm::vec3 right =
    app.view.camera.cameraOrientation * glm::vec3(1.0f, 0.0f, 0.0f);
  float pitchAngle = glm::radians(yoffset * sensitivity);
  glm::quat qPitch = glm::angleAxis(pitchAngle, right);

  app.view.camera.cameraOrientation =
    glm::normalize(qYaw * app.view.camera.cameraOrientation);
  app.view.camera.cameraOrientation =
    glm::normalize(app.view.camera.cameraOrientation * qPitch);

  glm::vec3 direction =
    app.view.camera.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
  app.view.camera.cameraPos =
    app.view.camera.cameraTarget - direction * app.view.camera.distance;
  app.view.camera.cameraUp =
    app.view.camera.cameraOrientation * glm::vec3(0.0f, 1.0f, 0.0f);
#else
  float sensitivity = 0.1f;
  float dx = xoffset * sensitivity;
  float dy = yoffset * sensitivity;

  yaw += dx;
  pitch += dy;

  if (pitch > 90.0f) {
    pitch = 180.0f - pitch;
    yaw += 180.0f;
  }
  if (pitch < -90.0f) {
    pitch = -180.0f - pitch;
    yaw += 180.0f;
  }
  if (pitch > 89.0f)  pitch = 89.0f;
  if (pitch < -89.0f) pitch = -89.0f;

  glm::vec3 direction;
  direction.x = cos(glm::radians(pitch)) * cos(glm::radians(yaw));
  direction.y = sin(glm::radians(pitch));
  direction.z = cos(glm::radians(pitch)) * sin(glm::radians(yaw));
  direction = glm::normalize(direction);

  app.view.camera.cameraPos =
    app.view.camera.cameraTarget - direction * app.view.camera.distance;
#endif
}

void scroll_callback(GLFWwindow* window, double /*xoffset*/, double yoffset)
{
  CallbackContext* ctx = GetCallbackContext(window);
  if (!ctx || !ctx->app)
    return;

  AppState& app = *ctx->app;

  if (ImGui::GetIO().WantCaptureMouse)
    return;

  ApplyCameraZoom(app.view.camera,
                  static_cast<float>(yoffset),
                  app.runtime.settings.minZoom,
                  app.runtime.settings.maxZoom);
}

void processInput(GLFWwindow* window)
{
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    glfwSetWindowShouldClose(window, true);
}

