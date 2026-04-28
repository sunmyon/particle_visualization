#include "interaction_utils.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

glm::vec3 MapToSphere(float x, float y,
                      float screenWidth,
                      float screenHeight)
{
  float centerX = screenWidth  * 0.5f;
  float centerY = screenHeight * 0.5f;
  float sizemax = std::max(screenWidth, screenHeight);

  float nx = 2.0f * (centerX - x) / sizemax;
  float ny = 2.0f * (centerY - y) / sizemax;
  float lengthSquared = nx * nx + ny * ny;

  if (lengthSquared <= 1.0f) {
    return glm::vec3(nx, ny, std::sqrt(1.0f - lengthSquared));
  }

  return glm::normalize(glm::vec3(nx, ny, 0.0f));
}

void ApplyCameraPan(CameraContext& camCtx,
                    float xoffset,
                    float yoffset)
{
  float panSensitivity = 0.005f * camCtx.distance;

  glm::vec3 forward = glm::normalize(camCtx.cameraTarget - camCtx.cameraPos);
  glm::vec3 rightVec = glm::normalize(glm::cross(forward, camCtx.cameraUp));
  glm::vec3 upVec = glm::normalize(glm::cross(rightVec, forward));

  glm::vec3 panOffset =
    (-xoffset * panSensitivity) * rightVec +
    (-yoffset * panSensitivity) * upVec;

  camCtx.cameraTarget += panOffset;
  camCtx.cameraPos += panOffset;
}

void ApplyCameraZoom(CameraContext& camCtx,
                     float yoffset,
                     float minZoom,
                     float maxZoom)
{
  float zoomSpeed = 0.1f * camCtx.distance;
  camCtx.distance += yoffset * zoomSpeed;

  if (camCtx.distance < minZoom) camCtx.distance = minZoom;
  if (camCtx.distance > maxZoom) camCtx.distance = maxZoom;

#if defined(ROTATE_ARCBALL) || defined(ROTATE_QUATERNION)
  glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
#else
  glm::vec3 direction = glm::normalize(camCtx.cameraTarget - camCtx.cameraPos);
#endif

  camCtx.cameraPos = camCtx.cameraTarget - direction * camCtx.distance;
}

void ApplyCameraArcballRotation(CameraContext& camCtx,
                                float oldX, float oldY,
                                float newX, float newY,
                                float screenWidth,
                                float screenHeight)
{
  const float xoffset = newX - oldX;
  const float yoffset = oldY - newY;

  // Preserve the legacy coordinate convention exactly.
  const float startX = newX - xoffset; // = oldX
  const float startY = newY - yoffset; // = 2*newY - oldY

  glm::vec3 startSphere = MapToSphere(startX, startY, screenWidth, screenHeight);
  glm::vec3 endSphere   = MapToSphere(newX, newY, screenWidth, screenHeight);

  glm::vec3 rotAxis = glm::cross(startSphere, endSphere);
  if (glm::length(rotAxis) <= 1.0e-5f)
    return;

  rotAxis = glm::normalize(rotAxis);

  float dotVal = glm::clamp(glm::dot(startSphere, endSphere), -1.0f, 1.0f);
  float angle = std::acos(dotVal);

  glm::quat qArcball = glm::angleAxis(angle, rotAxis);
  camCtx.cameraOrientation = glm::normalize(camCtx.cameraOrientation * qArcball);

  glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
  camCtx.cameraPos = camCtx.cameraTarget - direction * camCtx.distance;
  camCtx.cameraUp  = camCtx.cameraOrientation * glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::vec3 FlipLeftRight(const glm::vec3& v)
{
  return glm::vec3(-v.x, -v.y, v.z);
}

void UpdateCuboidTransformArcball(CuboidObject& cuboid,
                                  float oldX, float oldY,
                                  float newX, float newY,
                                  float screenWidth,
                                  float screenHeight,
                                  const glm::mat4& view,
                                  const glm::vec3& pivotWorld)
{
  glm::vec3 startSphere = MapToSphere(oldX, oldY, screenWidth, screenHeight);
  glm::vec3 endSphere   = MapToSphere(newX, newY, screenWidth, screenHeight);

  glm::vec3 rotAxis = glm::cross(startSphere, endSphere);
  rotAxis = FlipLeftRight(rotAxis);

  if (glm::length(rotAxis) <= 1.0e-5f)
    return;

  rotAxis = glm::normalize(rotAxis);

  float dotVal = glm::clamp(glm::dot(startSphere, endSphere), -1.0f, 1.0f);
  float angle = std::acos(dotVal);

  glm::mat4 invView = glm::inverse(view);
  glm::vec3 worldRotAxis =
    glm::normalize(glm::vec3(invView * glm::vec4(rotAxis, 0.0f)));

  glm::quat qArcball = glm::angleAxis(angle, worldRotAxis);

  glm::mat4 T    = glm::translate(glm::mat4(1.0f),  pivotWorld);
  glm::mat4 Tinv = glm::translate(glm::mat4(1.0f), -pivotWorld);
  glm::mat4 R    = glm::mat4_cast(qArcball);

  glm::mat4 oldMat =
    glm::translate(glm::mat4(1.0f), cuboid.center) *
    glm::mat4_cast(cuboid.orientation);

  glm::mat4 newMat = T * R * Tinv * oldMat;

  cuboid.center = glm::vec3(newMat[3].x, newMat[3].y, newMat[3].z);
  cuboid.orientation = glm::normalize(glm::quat_cast(glm::mat3(newMat)));
}
