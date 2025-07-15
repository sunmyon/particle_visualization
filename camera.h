#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

// ------------------------------
// カメラ関連グローバル変数（オービット方式）
// ------------------------------
struct CameraContext {
#ifdef ROTATE_QUATERNION
  glm::quat cameraOrientation  = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};  // 初期状態は glm::quat(1,0,0,0) （単位四元数）
#endif
  glm::vec3 cameraTarget{0.0f, 0.0f, 0.0f};
  glm::vec3 cameraPos{0.0f, 0.0f, 5.0f};
  glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
  
  float distance = 5.0f;
#if !defined(ROTATE_ARCBALL) && !defined(ROTATE_QUATERNION)
  float yaw   = -90.0f;
  float pitch = 0.0f;
#endif
  bool stopCameraMode = false;  // 直方体回転モードのオン/オフフラグ

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

