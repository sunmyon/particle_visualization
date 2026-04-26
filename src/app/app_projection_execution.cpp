#include "app/app_projection_execution.h"

#include "app/analysis_state.h"
#include "app/normalization_config.h"
#include "app/render_runtime_state.h"
#include "data/particle_array.h"
#include "image/image_io.h"
#include "interaction/camera.h"
#include "interaction/interaction_utils.h"
#include "projection/make_2D_projection_map.h"
#include "projection/projection_map_tool_state.h"
#include "projection/projection_map_context.h"
#include "projection/projection_geometry.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

static bool IsSafeIndexFormat(const char* format)
{
  if (!format || format[0] == '\0') {
    return false;
  }

  bool hasIndexSpecifier = false;
  const char* p = format;
  while (*p) {
    if (*p != '%') {
      ++p;
      continue;
    }

    ++p;
    if (*p == '\0') {
      return false;
    }
    if (*p == '%') {
      ++p;
      continue;
    }

    while (*p == '-' || *p == '+' || *p == ' ' || *p == '0' || *p == '#') ++p;
    while (*p >= '0' && *p <= '9') ++p;
    if (*p == '.') {
      ++p;
      while (*p >= '0' && *p <= '9') ++p;
    }
    if (*p == 'h' || *p == 'l' || *p == 'j' || *p == 'z' || *p == 't') {
      const char m = *p++;
      if ((m == 'h' && *p == 'h') || (m == 'l' && *p == 'l')) {
        ++p;
      }
    }

    if (*p == '\0') {
      return false;
    }

    if (*p == 'd' || *p == 'i' || *p == 'u') {
      hasIndexSpecifier = true;
      ++p;
      continue;
    }

    return false;
  }

  return hasIndexSpecifier;
}

static void StoreQuatAsEulerDegrees(const glm::quat& q, float outEuler[3])
{
  const glm::vec3 euler = glm::degrees(glm::eulerAngles(glm::normalize(q)));
  outEuler[0] = euler.x;
  outEuler[1] = euler.y;
  outEuler[2] = euler.z;
}

static void SyncInteractiveCuboidFromParams(ProjectionMapToolState& tool)
{
  tool.interactiveCuboid.center = glm::vec3(tool.params.xoffset[0],
                                            tool.params.xoffset[1],
                                            tool.params.xoffset[2]);
  tool.interactiveCuboid.halfSize = 0.5f * glm::vec3(tool.params.xlen[0],
                                                     tool.params.xlen[1],
                                                     tool.params.xlen[2]);
  tool.interactiveCuboid.orientation =
    BuildProjectionTransformFromEuler(tool.params.tilt);
  tool.appliedSelectedAxis = tool.params.selectedAxis;
}

static void SyncParamsFromInteractiveCuboid(ProjectionMapToolState& tool)
{
  tool.params.xoffset[0] = tool.interactiveCuboid.center.x;
  tool.params.xoffset[1] = tool.interactiveCuboid.center.y;
  tool.params.xoffset[2] = tool.interactiveCuboid.center.z;
  StoreQuatAsEulerDegrees(tool.interactiveCuboid.orientation, tool.params.tilt);
}

static void MarkProjectionToolChanged(ProjectionMapToolState& tool,
                                      RenderLayerState& cuboidAnnotation)
{
  ++tool.revision;
  cuboidAnnotation.show = tool.params.flagShowCuboid;
  cuboidAnnotation.cpuUpdated = true;
}

void ExecuteProjectionMapRequests(ProjectionMapRequestState& request,
                                  ProjectionMapToolState& tool,
                                  ProjectionMapGenerator& generator,
                                  ParticleArray& particles,
				  const UnitSystem& units,
                                  const NormalizationContext& normalization,
                                  const CameraContext& camera,
                                  RenderLayerState& cuboidAnnotation,
				  int currentFileIndex,
                                  ProjectionPreviewDerivedState& preview,
				  double time)
{
  if (request.paramsChanged) {
    tool.params = request.params;
    SyncInteractiveCuboidFromParams(tool);
    MarkProjectionToolChanged(tool, cuboidAnnotation);
    request.paramsChanged = false;
  }

  if (request.moveCenterToCameraRequested) {
    tool.params.xoffset[0] = camera.cameraTarget.x;
    tool.params.xoffset[1] = camera.cameraTarget.y;
    tool.params.xoffset[2] = camera.cameraTarget.z;
    SyncInteractiveCuboidFromParams(tool);
    MarkProjectionToolChanged(tool, cuboidAnnotation);
    request.moveCenterToCameraRequested = false;
  }

  if (request.setAxisFromAngularMomentumRequested) {
    ProjectionAngularMomentumFrame frame =
      ComputeAngularMomentumFrame(particles.particleBlock.particles,
                                  glm::vec3(tool.params.xoffset[0],
                                            tool.params.xoffset[1],
                                            tool.params.xoffset[2]),
                                  tool.params.xlen);
    if (frame.valid) {
      tool.params.xoffset[0] = frame.center.x;
      tool.params.xoffset[1] = frame.center.y;
      tool.params.xoffset[2] = frame.center.z;
      StoreQuatAsEulerDegrees(BuildRotationFromZAxisTo(frame.axis),
                              tool.params.tilt);
      SyncInteractiveCuboidFromParams(tool);
      MarkProjectionToolChanged(tool, cuboidAnnotation);
    }
    request.setAxisFromAngularMomentumRequested = false;
  }

  if (request.arcballDragRequested) {
    const glm::mat4 view =
      glm::lookAt(camera.cameraPos, camera.cameraTarget, camera.cameraUp);
    UpdateCuboidTransformArcball(tool.interactiveCuboid,
                                 request.dragOldX,
                                 request.dragOldY,
                                 request.dragNewX,
                                 request.dragNewY,
                                 request.displayWidth,
                                 request.displayHeight,
                                 view,
                                 tool.interactiveCuboid.center);
    SyncParamsFromInteractiveCuboid(tool);
    MarkProjectionToolChanged(tool, cuboidAnnotation);
    request.arcballDragRequested = false;
  }

  if (!request.renderRequested) return;

  ProjectionMapParams& params = tool.params;

  ProjectionMapContext context =
    BuildProjectionMapContext(params,
                              normalization.toPhysicalScale(),
                              time);
  
  RgbImage image =
    generator.makeDensityMapImage(particles,
				  units,
                                  params,
                                  context);

  char pattern[512];
  std::snprintf(pattern,
                sizeof(pattern),
                "%s/%s",
                params.folderPath,
                params.fileFormat);
  
  char filename[512];
  if (IsSafeIndexFormat(params.fileFormat)) {
    std::snprintf(filename,
                  sizeof(filename),
                  pattern,
                  currentFileIndex);
  } else {
    if (std::strchr(params.fileFormat, '%') != nullptr) {
      std::cerr << "Unsafe projection file format. Falling back to literal filename: "
                << params.fileFormat << "\n";
    }
    std::snprintf(filename,
                  sizeof(filename),
                  "%s/%s",
                  params.folderPath,
                  params.fileFormat);
  }

  if (!WritePngRgb(filename, image.width, image.height, image.rgb)) {
    std::cerr << "Failed to write projection map: " << filename << "\n";
  }

  preview.image = std::move(image);
  preview.valid = preview.image.valid();
  if (preview.valid) {
    preview.version += 1;
    preview.image.version = preview.version;
    preview.computed = true;
  } else {
    preview.computed = false;
  }
  
  request.renderRequested = false;
}
