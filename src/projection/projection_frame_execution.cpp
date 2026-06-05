#include "projection/projection_frame_execution.h"

#include "image/image_io.h"
#include "data/simulation_block.h"
#include "projection/make_2D_projection_map.h"
#include "projection/projection_map_context.h"

#include <cstdio>
#include <cstring>
#include <utility>

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

std::string ResolveProjectionMapOutputPath(const ProjectionMapParams& params,
                                           int currentFileIndex,
                                           std::string* warning)
{
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
    if (std::strchr(params.fileFormat, '%') != nullptr && warning) {
      *warning = "Unsafe projection file format. Falling back to literal filename: ";
      *warning += params.fileFormat;
    }
    std::snprintf(filename,
                  sizeof(filename),
                  "%s/%s",
                  params.folderPath,
                  params.fileFormat);
  }

  return filename;
}

ProjectionFrameResult ExecuteProjectionFrame(ProjectionFrameExecutionContext& projection,
                                             ProjectionMapParams params,
                                             double time,
                                             ProjectionFrameOutputOptions output)
{
  ProjectionFrameResult result;
  QuantityStateScope quantityScope(projection.quantity);
  ProjectionMapContext context =
    BuildProjectionMapContext(params,
                              time);
  result.outputPath = std::move(output.path);

  result.image =
    projection.generator.makeDensityMapImage(projection.particles,
                                             projection.units,
                                             params,
                                             context);

  if (!result.image.valid()) {
    result.error = "Failed to generate projection map image.";
    return result;
  }

  if (output.writeFile) {
    if (result.outputPath.empty()) {
      result.error = "Projection map output path is empty.";
      if (!output.keepImage) {
        result.image.clear();
      }
      return result;
    }

    if (!WritePngRgb(result.outputPath.c_str(),
                     result.image.width,
                     result.image.height,
                     result.image.rgb)) {
      result.error = "Failed to write projection map: " + result.outputPath;
      if (!output.keepImage) {
        result.image.clear();
      }
      return result;
    }
  }

  result.ok = true;
  if (!output.keepImage) {
    result.image.clear();
  }
  return result;
}
