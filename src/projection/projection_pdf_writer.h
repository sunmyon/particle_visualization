#pragma once

#include "image/rgb_image.h"
#include "projection/make_2D_projection_map.h"
#include "projection/projection_map_context.h"
#include "projection/projection_map_params.h"

#include <string>

bool WriteProjectionPdf(const std::string& path,
                        const RgbImage& plotImage,
                        const ProjectionMapParams& params,
                        const ProjectionMapContext& ctx,
                        const ProjectionMapRenderInfo& info);
