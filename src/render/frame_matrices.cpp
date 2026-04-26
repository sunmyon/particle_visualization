#include "frame_matrices.h"

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

  fm.projection = glm::perspective(glm::radians(fovY), aspect, 0.1f, 1000.0f);
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
