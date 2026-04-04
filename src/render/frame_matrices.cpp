#include "frame_matrices.h"

#include <glm/gtc/matrix_transform.hpp>

#include "interaction/camera.h"
#include "window_context.h"

FrameMatrices BuildFrameMatrices(const CameraContext& camCtx,
                                 const WindowContext& windowCtx)
{
  FrameMatrices fm;

  fm.view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);

  const float fovY = 45.0f;

#ifdef USE_LETTERBOX
  const float targetAspect = 1280.0f / 720.0f;
  fm.projection = glm::perspective(glm::radians(fovY), targetAspect, 0.1f, 1000.0f);
  fm.viewportW = windowCtx.viewportWidth();
  fm.viewportH = windowCtx.viewportHeight();
#else
  int width, height;
  glfwGetFramebufferSize(windowCtx.handle(), &width, &height);
  float aspect = static_cast<float>(width) / static_cast<float>(height);
  fm.projection = glm::perspective(glm::radians(fovY), aspect, 0.1f, 1000.0f);
  fm.viewportW = width;
  fm.viewportH = height;
#endif

  fm.invProj = glm::inverse(fm.projection);
  fm.invView = glm::inverse(fm.view);
  fm.camForward =
      glm::normalize(glm::vec3(fm.view[0][2], fm.view[1][2], fm.view[2][2])) * -1.0f;

  auto focalPxFromFovY = [](int H, float fovYdeg) {
    const float fovYrad = glm::radians(fovYdeg);
    return (H > 0) ? (0.5f * static_cast<float>(H)) / tanf(0.5f * fovYrad) : 1.0f;
  };
  fm.focalPx = focalPxFromFovY(fm.viewportH, fovY);

  return fm;
}
