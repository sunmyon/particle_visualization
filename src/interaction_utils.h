#pragma once

#include "object.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

glm::vec3 FlipLeftRight(const glm::vec3& v);

void UpdateCuboidTransformArcball(CuboidObject& cuboid,
                                  float lastX, float lastY,
                                  float xpos, float ypos,
				  float width, float height,
                                  const glm::mat4& view,
                                  const glm::vec3& pivotWorld);

glm::vec3 mapToSphere(float x, float y, float width, float height);
