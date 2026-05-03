#include "projection/metal_projection_backend.h"

#include <cstdlib>
#include <iostream>

int main()
{
  const char* countEnv = std::getenv("PARTICLE_VIS_METAL_PROJECTION_SMOKE_COUNT");
  const int particleCount = countEnv ? std::max(1, std::atoi(countEnv)) : 1;

  MetalProjectionMapInput input;
  input.width = 32;
  input.height = 32;
  input.dx = 1.0f / 32.0f;
  input.dy = 1.0f / 32.0f;
  input.xminLocal[0] = -0.5f;
  input.xminLocal[1] = -0.5f;
  input.center = glm::vec3(0.0f);
  input.uAxis = glm::vec3(1.0f, 0.0f, 0.0f);
  input.vAxis = glm::vec3(0.0f, 1.0f, 0.0f);

  MetalProjectionParticle particle;
  particle.pos[0] = 0.0f;
  particle.pos[1] = 0.0f;
  particle.pos[2] = 0.0f;
  particle.val = 2.0f;
  particle.density = 1.0f;
  particle.mass = 1.0f;
  particle.hsml = 0.1f;
  input.particles.resize(static_cast<std::size_t>(particleCount), particle);
  for (int i = 0; i < particleCount; ++i) {
    const float t = static_cast<float>(i % 1024) / 1023.0f;
    input.particles[static_cast<std::size_t>(i)].pos[0] = -0.25f + 0.5f * t;
    input.particles[static_cast<std::size_t>(i)].pos[1] =
      -0.25f + 0.5f * static_cast<float>((i / 1024) % 1024) / 1023.0f;
  }

  MetalProjectionMapOutput output;
  if (!RunMetalProjectionMap(input, output)) {
    std::cerr << "metal_projection_smoke failed\n";
    return 1;
  }

  std::size_t nonzero = 0;
  double weightSum = 0.0;
  for (float weight : output.weights) {
    if (weight > 0.0f) {
      ++nonzero;
      weightSum += weight;
    }
  }
  std::cout << "metal_projection_smoke nonzero=" << nonzero
            << " weightSum=" << weightSum << "\n";
  return nonzero > 0 ? 0 : 1;
}
