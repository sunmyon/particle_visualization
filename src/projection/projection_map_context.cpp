#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "projection/projection_map_params.h"
#include "projection/projection_map_context.h"
#include "render/colormap_defs.h"

#include <algorithm>

ProjectionMapContext BuildProjectionMapContext(const ProjectionMapParams& params,
                                               double scaleToPhysical,
                                               double time)
{
  ProjectionMapContext ctx;

  ctx.selectedType = static_cast<char>(params.selectedType);

  ctx.center = glm::vec3(params.xoffset[0],
                         params.xoffset[1],
                         params.xoffset[2]);

  ctx.cuboidTransform = BuildProjectionTransformFromEuler(params.tilt);

  glm::vec3 axes[3] = {
    glm::normalize(ctx.cuboidTransform * glm::vec3(1.0f, 0.0f, 0.0f)),
    glm::normalize(ctx.cuboidTransform * glm::vec3(0.0f, 1.0f, 0.0f)),
    glm::normalize(ctx.cuboidTransform * glm::vec3(0.0f, 0.0f, 1.0f))
  };
  const int selectedAxis = std::clamp(params.selectedAxis, 0, 2);
  const float projectionSign = params.projectionSign < 0 ? -1.0f : 1.0f;
  ctx.planeNormal = glm::normalize(projectionSign * axes[selectedAxis]);

  int colormapIndex = params.colormapindex;
  const ColormapDef* colormaps = AvailableColormaps();
  const int colormapCount = AvailableColormapCount();
  if (colormapIndex < 0) {
    colormapIndex = 0;
  }
  if (colormapIndex >= colormapCount) {
    colormapIndex = colormapCount - 1;
  }

  ctx.colorMap = colormaps[colormapIndex].data;
  ctx.colorMapSize = colormaps[colormapIndex].count;

  ctx.scaleToPhysical = scaleToPhysical;
  ctx.time = time;

  return ctx;
}

glm::quat BuildProjectionTransformFromEuler(const float* eulerAngles)
{
  glm::vec3 eulerRad = glm::radians(glm::vec3(eulerAngles[0],
                                              eulerAngles[1],
                                              eulerAngles[2]));

  glm::quat qx = glm::angleAxis(eulerRad.x, glm::vec3(1.0f, 0.0f, 0.0f));
  glm::quat qy = glm::angleAxis(eulerRad.y, glm::vec3(0.0f, 1.0f, 0.0f));
  glm::quat qz = glm::angleAxis(eulerRad.z, glm::vec3(0.0f, 0.0f, 1.0f));

  return qz * qy * qx;
}
