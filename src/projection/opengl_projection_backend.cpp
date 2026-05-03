#include "projection/opengl_projection_backend.h"

#include <iostream>

bool IsOpenGLProjectionBackendAvailable(std::string* reason)
{
  if (reason) {
    *reason =
      "OpenGL projection compute is intentionally disabled in the current "
      "OpenGL path. The application requests an OpenGL 3.3 context for window "
      "rendering, while exact SPH projection needs compute shaders plus "
      "float atomic accumulation. macOS OpenGL also cannot provide this path.";
  }
  return false;
}

bool RunOpenGLProjectionMap(const ProjectionGpuMapInput& input,
                            ProjectionGpuMapOutput& output)
{
  (void)input;
  output = ProjectionGpuMapOutput{};
  std::string reason;
  IsOpenGLProjectionBackendAvailable(&reason);
  std::cerr << "OpenGL projection backend unavailable: " << reason
            << std::endl;
  return false;
}

bool RunOpenGLVoronoiProjectionMap(const ProjectionGpuMapInput& input,
                                   ProjectionGpuMapOutput& output)
{
  (void)input;
  output = ProjectionGpuMapOutput{};
  std::string reason;
  IsOpenGLProjectionBackendAvailable(&reason);
  std::cerr << "OpenGL Voronoi projection backend unavailable: " << reason
            << std::endl;
  return false;
}
