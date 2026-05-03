#pragma once

#include "projection/projection_gpu_backend.h"

#include <string>

bool IsOpenGLProjectionBackendAvailable(std::string* reason = nullptr);

bool RunOpenGLProjectionMap(const ProjectionGpuMapInput& input,
                            ProjectionGpuMapOutput& output);

bool RunOpenGLVoronoiProjectionMap(const ProjectionGpuMapInput& input,
                                   ProjectionGpuMapOutput& output);
