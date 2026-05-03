#pragma once

#include "projection/projection_gpu_backend.h"

#include <string>

class VulkanContext;

void SetVulkanProjectionContext(VulkanContext* context);

bool IsVulkanProjectionBackendAvailable(std::string* reason = nullptr);

bool RunVulkanProjectionMap(const ProjectionGpuMapInput& input,
                            ProjectionGpuMapOutput& output);

bool BuildVulkanVoronoiLabelGrid(const ProjectionGpuMapInput& input,
                                 ProjectionGpuLabelGrid& grid);

bool IntegrateVulkanVoronoiLabelGrid(const ProjectionGpuMapInput& input,
                                     const ProjectionGpuLabelGrid& grid,
                                     ProjectionGpuMapOutput& output);

bool RunVulkanVoronoiProjectionMap(const ProjectionGpuMapInput& input,
                                   ProjectionGpuMapOutput& output);
