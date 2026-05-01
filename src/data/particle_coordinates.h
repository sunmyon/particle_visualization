#pragma once

#include <glm/vec3.hpp>

#include "data/particle_data.h"

inline glm::vec3 normalizedParticlePosition(const ParticleData& p,
                                            float normalizedScale)
{
  return glm::vec3(p.original_pos[0] * normalizedScale,
                   p.original_pos[1] * normalizedScale,
                   p.original_pos[2] * normalizedScale);
}

inline void normalizedParticlePosition(const ParticleData& p,
                                       float normalizedScale,
                                       float out[3])
{
  out[0] = p.original_pos[0] * normalizedScale;
  out[1] = p.original_pos[1] * normalizedScale;
  out[2] = p.original_pos[2] * normalizedScale;
}

inline float normalizedParticleHsml(const ParticleData& p,
                                    float normalizedScale)
{
  return p.original_hsml * normalizedScale;
}
