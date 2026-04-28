#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ------------------------------
// Camera-related state for orbit-style controls.
// ------------------------------
struct CameraContext {
#ifdef ROTATE_QUATERNION
  glm::quat cameraOrientation  = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};  // Initial unit quaternion.
#endif
  glm::vec3 cameraTarget{0.0f, 0.0f, 0.0f};
  glm::vec3 cameraPos{0.0f, 0.0f, 5.0f};
  glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
  
  float distance = 5.0f;
#if !defined(ROTATE_ARCBALL) && !defined(ROTATE_QUATERNION)
  float yaw   = -90.0f;
  float pitch = 0.0f;
#endif
  bool stopCameraMode = false;  // Enables or disables cuboid rotation mode.

  CameraContext(const glm::vec3& target = {0.0f, 0.0f, 0.0f},
		const glm::vec3& pos    = {0.0f, 0.0f, 5.0f},
		const glm::vec3& up     = {0.0f, 1.0f, 0.0f},
		float dist = 5.0f
#if !defined(ROTATE_ARCBALL) && !defined(ROTATE_QUATERNION)
		, float yaw_ = -90.0f
		, float pitch_ = 0.0f
#endif
		)
    : cameraTarget(target), cameraPos(pos), cameraUp(up), distance(dist)
#if !defined(ROTATE_ARCBALL) && !defined(ROTATE_QUATERNION)
    , yaw(yaw_), pitch(pitch_)
#endif
  { }
};
