#pragma once

#include "projection/projection_gpu_backend.h"

using MetalProjectionParticle = ProjectionGpuParticle;
using MetalProjectionMapInput = ProjectionGpuMapInput;
using MetalProjectionMapOutput = ProjectionGpuMapOutput;
using MetalVoronoiLabelGrid = ProjectionGpuLabelGrid;

bool RunMetalProjectionMap(const MetalProjectionMapInput& input,
                           MetalProjectionMapOutput& output);

bool RunMetalVoronoiProjectionMap(const MetalProjectionMapInput& input,
                                  MetalProjectionMapOutput& output);

bool BuildMetalVoronoiLabelGrid(const MetalProjectionMapInput& input,
                                MetalVoronoiLabelGrid& grid);

bool IntegrateMetalVoronoiLabelGrid(const MetalProjectionMapInput& input,
                                    const MetalVoronoiLabelGrid& grid,
                                    MetalProjectionMapOutput& output);
