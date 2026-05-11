#include "frame_matrices.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "interaction/camera.h"
#include "render/render_viewport.h"

FrameMatrices BuildFrameMatrices(const CameraContext& camCtx,
                                 const RenderViewport& viewport)
{
  FrameMatrices fm;
  fm.view = glm::lookAt(camCtx.cameraPos, camCtx.cameraTarget, camCtx.cameraUp);

  const float fovY = 45.0f;
  const float aspect = viewport.aspect();

  float cameraDistance = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
  if (!std::isfinite(cameraDistance) || cameraDistance <= 0.0f) {
    cameraDistance = std::max(camCtx.distance, 1.0f);
  }
  fm.nearClip = std::clamp(cameraDistance * 1.0e-4f, 1.0e-5f, 0.1f);
  fm.farClip = std::max(1000.0f, cameraDistance * 1000.0f);
  fm.projection =
    glm::perspective(glm::radians(fovY), aspect, fm.nearClip, fm.farClip);
  fm.viewportW = viewport.width;
  fm.viewportH = viewport.height;

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
