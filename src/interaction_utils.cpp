#include "interaction_utils.h"

#include "object.h"

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

glm::vec3 mapToSphere(float x, float y, float width, float height) {
  float centerX = width * 0.5f;
  float centerY = height * 0.5f;
  float sizemax = std::max(width, height);
  
  // 画面中心を原点にして -1～1 に正規化
  //float nx = 2.0f * (x - centerX) / screenWidth;
  float nx = 2.0f * (centerX - x) / sizemax;
  float ny = 2.0f * (centerY - y) / sizemax; // Y座標は上方向が正になるように
  //float ny = (2.0f * y - screenWidth) / screenHeight; // Y座標は上方向が正になるように
  float lengthSquared = nx * nx + ny * ny;

  glm::vec3 result;
  if (lengthSquared <= 1.0f) {
    // 球面上の点：z = sqrt(1 - x^2 - y^2)
    result = glm::vec3(nx, ny, sqrt(1.0f - lengthSquared));
  } else {
    // 球の外側の場合は正規化（楕円状に補正）
    result = glm::normalize(glm::vec3(nx, ny, 0.0f));
  }

  return result;
}


glm::vec3 FlipLeftRight(const glm::vec3& v)
{
  return glm::vec3(-v.x, -v.y, v.z);
}

void UpdateCuboidTransformArcball(CuboidObject& cuboid,
                                  float lastX,
                                  float lastY,
                                  float xpos,
                                  float ypos,
				  float screenWidth, float screenHeight,				  
                                  const glm::mat4& view,
                                  const glm::vec3& pivotWorld)
{
  glm::vec3 startSphere = mapToSphere(lastX, lastY, screenWidth, screenHeight);
  glm::vec3 endSphere   = mapToSphere(xpos,  ypos,  screenWidth, screenHeight);

  glm::vec3 rotAxis = glm::cross(startSphere, endSphere);
  rotAxis = FlipLeftRight(rotAxis);

  if (glm::length(rotAxis) <= 1.0e-5f)
    return;

  rotAxis = glm::normalize(rotAxis);

  float dotVal = glm::clamp(glm::dot(startSphere, endSphere), -1.0f, 1.0f);
  float angle = std::acos(dotVal);

  float sensitivity = 1.0f;
  angle *= sensitivity;

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

  glm::vec3 newCenter = glm::vec3(newMat[3]);
  glm::mat3 rot3x3    = glm::mat3(newMat);
  glm::quat newRot    = glm::quat_cast(rot3x3);

  cuboid.center      = newCenter;
  cuboid.orientation = glm::normalize(newRot);
}
