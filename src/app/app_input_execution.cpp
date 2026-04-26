#include "app/app_input_execution.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "app/runtime_state.h"
#include "interaction/camera.h"
#include "interaction/interaction_utils.h"

namespace {

void ResetPointerIfNeeded(InteractionState& interaction,
                          const InputEvent& event)
{
  if (event.type == InputEventType::PointerMove) {
    interaction.resetMouse();
  }
}

void ApplyPointerMove(const InputEvent& event,
                      InteractionState& interaction,
                      CameraContext& camera)
{
  if (!event.primaryDown) {
    interaction.resetMouse();
    return;
  }

  if (interaction.firstMouse()) {
    interaction.setMousePosition(event.x, event.y);
    return;
  }

  const float oldX = interaction.lastX();
  const float oldY = interaction.lastY();
  const float xoffset = event.x - oldX;
  const float yoffset = oldY - event.y;

  interaction.setMousePosition(event.x, event.y);

  if (event.modifiers.shift) {
    ApplyCameraPan(camera, xoffset, yoffset);
    return;
  }

#if defined(ROTATE_ARCBALL)
  ApplyCameraArcballRotation(camera,
                             oldX, oldY,
                             event.x, event.y,
                             static_cast<float>(event.viewport.width),
                             static_cast<float>(event.viewport.height));
#elif defined(ROTATE_QUATERNION)
  const float sensitivity = 0.1f;

  const float yawAngle = glm::radians(-xoffset * sensitivity);
  const glm::quat qYaw =
    glm::angleAxis(yawAngle, glm::vec3(0.0f, 1.0f, 0.0f));

  const glm::vec3 right =
    camera.cameraOrientation * glm::vec3(1.0f, 0.0f, 0.0f);
  const float pitchAngle = glm::radians(yoffset * sensitivity);
  const glm::quat qPitch = glm::angleAxis(pitchAngle, right);

  camera.cameraOrientation =
    glm::normalize(qYaw * camera.cameraOrientation);
  camera.cameraOrientation =
    glm::normalize(camera.cameraOrientation * qPitch);

  const glm::vec3 direction =
    camera.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
  camera.cameraPos =
    camera.cameraTarget - direction * camera.distance;
  camera.cameraUp =
    camera.cameraOrientation * glm::vec3(0.0f, 1.0f, 0.0f);
#else
  const float sensitivity = 0.1f;
  const float dx = xoffset * sensitivity;
  const float dy = yoffset * sensitivity;

  camera.yaw += dx;
  camera.pitch += dy;

  if (camera.pitch > 90.0f) {
    camera.pitch = 180.0f - camera.pitch;
    camera.yaw += 180.0f;
  }
  if (camera.pitch < -90.0f) {
    camera.pitch = -180.0f - camera.pitch;
    camera.yaw += 180.0f;
  }
  camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);

  glm::vec3 direction;
  direction.x = std::cos(glm::radians(camera.pitch)) *
                std::cos(glm::radians(camera.yaw));
  direction.y = std::sin(glm::radians(camera.pitch));
  direction.z = std::cos(glm::radians(camera.pitch)) *
                std::sin(glm::radians(camera.yaw));
  direction = glm::normalize(direction);

  camera.cameraPos =
    camera.cameraTarget - direction * camera.distance;
#endif
}

} // namespace

InputExecutionResult ExecuteInputEvents(InputEventQueue& input,
                                        InteractionState& interaction,
                                        CameraContext& camera,
                                        const SettingsRuntimeState& settings)
{
  InputExecutionResult result;
  const std::vector<InputEvent> events = input.drain();

  for (const InputEvent& event : events) {
    if (event.type == InputEventType::Key) {
      if (event.key == InputKey::Escape &&
          event.action == InputAction::Press) {
        result.closeRequested = true;
      }
      continue;
    }

    if (event.type == InputEventType::FramebufferResize) {
      continue;
    }

    if (event.capturedByUI ||
        (event.type == InputEventType::PointerMove && camera.stopCameraMode)) {
      ResetPointerIfNeeded(interaction, event);
      continue;
    }

    switch (event.type) {
    case InputEventType::PointerMove:
      ApplyPointerMove(event, interaction, camera);
      break;

    case InputEventType::PointerScroll:
      ApplyCameraZoom(camera,
                      event.wheelY,
                      settings.minZoom,
                      settings.maxZoom);
      break;

    case InputEventType::Key:
    case InputEventType::FramebufferResize:
      break;
    }
  }

  return result;
}
