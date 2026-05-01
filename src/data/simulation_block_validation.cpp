#include "data/simulation_block_validation.h"

#include "core/quantity.h"
#include "data/simulation_block.h"

#include <cmath>
#include <cstdio>

namespace {
bool Finite3(const float v[3])
{
  return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

SimulationBlockValidationResult Invalid(std::size_t index, const char* reason)
{
  SimulationBlockValidationResult result;
  result.valid = false;
  result.particleIndex = index;
  std::snprintf(result.message,
                sizeof(result.message),
                "invalid particle at index %zu: %s",
                index,
                reason);
  return result;
}
}

SimulationBlockValidationResult ValidateSimulationBlock(const SimulationBlock& block)
{
  SimulationBlockValidationResult result;

  for (std::size_t i = 0; i < block.particles.size(); ++i) {
    const SimulationElement& p = block.particles[i];
    const int type = static_cast<int>(p.type);
    if (type < 0 || type >= kNumTypes) {
      return Invalid(i, "particle type is outside [0, kNumTypes)");
    }

    if (!Finite3(p.position)) {
      return Invalid(i, "position contains non-finite value");
    }

    if (!Finite3(p.vel)) {
      return Invalid(i, "vel contains non-finite value");
    }

    if (!std::isfinite(p.supportRadius) || p.supportRadius < 0.0f) {
      return Invalid(i, "supportRadius is non-finite or negative");
    }

    if (!std::isfinite(p.mass)) {
      return Invalid(i, "mass is non-finite");
    }
  }

  return result;
}
