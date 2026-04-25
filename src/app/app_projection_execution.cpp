#include "app/app_projection_execution.h"

#include "app/analysis_state.h"
#include "app/normalization_config.h"
#include "data/particle_array.h"
#include "image/image_io.h"
#include "projection/make_2D_projection_map.h"
#include "projection/projection_map_tool_state.h"
#include "projection/projection_map_context.h"

#include <cstdio>
#include <cstring>
#include <iostream>
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

void ExecuteProjectionMapRequests(ProjectionMapToolState& tool,
                                  ProjectionMapGenerator& generator,
                                  ParticleArray& particles,
				  const UnitSystem& units,
                                  const NormalizationContext& normalization,
				  int currentFileIndex,
                                  ProjectionPreviewDerivedState& preview,
				  double time)
{
  if (!tool.renderRequested) return;

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
  
  tool.renderRequested = false;
}
