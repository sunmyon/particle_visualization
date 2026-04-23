#include "app/app_projection_execution.h"

#include "app/analysis_state.h"
#include "app/normalization_config.h"
#include "data/particle_array.h"
#include "image/image_io.h"
#include "projection/make_2D_projection_map.h"
#include "projection/projection_map_tool_state.h"

#include <cstdio>
#include <iostream>
#include <utility>

void ExecuteProjectionMapRequests(ProjectionMapToolState& tool,
                                  ProjectionMapGenerator& generator,
                                  ParticleArray& particles,
                                  const NormalizationContext& normalization,
                                  ProjectionPreviewDerivedState& preview)
{
  if (!tool.renderRequested) return;

  ProjectionMapParams& params = tool.params;

  ProjectionMapContext context =
    BuildProjectionMapContext(params,
                              normalization.toPhysicalScale(),
                              particles.particleBlock.header.time);
  
  RgbImage image =
    generator.makeDensityMapImage(particles,
                                  params,
                                  context);

  char filename[512];
  std::snprintf(filename,
                sizeof(filename),
                "%s/%s",
                params.folderPath,
                params.fileFormat);

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
