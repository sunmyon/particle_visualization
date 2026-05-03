#include "projection/vulkan_projection_backend.h"

#include <cmath>
#include <iostream>

int main()
{
  ProjectionGpuMapInput input;
  input.width = 16;
  input.height = 16;
  input.depth = 16;
  input.dx = 1.0f / static_cast<float>(input.width);
  input.dy = 1.0f / static_cast<float>(input.height);
  input.dz = 1.0f / static_cast<float>(input.depth);
  input.xminLocal[0] = -0.5f;
  input.xminLocal[1] = -0.5f;
  input.xminLocal[2] = -0.5f;

  ProjectionGpuParticle a;
  a.pos[0] = -0.2f;
  a.pos[1] = 0.0f;
  a.pos[2] = 0.0f;
  a.val = 2.0f;
  a.density = 1.0f;
  a.mass = 1.0f;
  a.hsml = 0.1f;
  input.particles.push_back(a);

  ProjectionGpuParticle b = a;
  b.pos[0] = 0.2f;
  b.val = 4.0f;
  input.particles.push_back(b);

  ProjectionGpuLabelGrid grid;
  if (!BuildVulkanVoronoiLabelGrid(input, grid)) {
    std::cerr << "vulkan_projection_smoke failed during label build\n";
    return 1;
  }

  ProjectionGpuMapOutput output;
  if (!IntegrateVulkanVoronoiLabelGrid(input, grid, output)) {
    std::cerr << "vulkan_projection_smoke failed during integration\n";
    return 1;
  }

  std::size_t nonzero = 0;
  double weightSum = 0.0;
  for (float weight : output.weights) {
    if (weight > 0.0f && std::isfinite(weight)) {
      ++nonzero;
      weightSum += weight;
    }
  }
  std::cout << "vulkan_projection_smoke nonzero=" << nonzero
            << " weightSum=" << weightSum << "\n";
  return nonzero > 0 ? 0 : 1;
}
