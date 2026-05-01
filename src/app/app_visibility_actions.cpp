#include "app/app_visibility_actions.h"
#include "app/state/view_filter_config.h"

#include "data/particle_array.h"
#include "data/particle_coordinates.h"
#include "interaction/camera.h"

void ApplyCullingSphere(ParticleArray& particles,
                        const ViewFilterConfig& viewFilter)
{
  const float originalToNormalized =
    particles.particleBlock.normalizedScale > 0.0f
      ? particles.particleBlock.normalizedScale
      : 1.0f;
  const double radius = viewFilter.radiusCullingSphere * originalToNormalized;
  const glm::vec3 center = viewFilter.center * originalToNormalized;
  
  for (size_t i = 0; i < particles.particleBlock.particles.size(); ++i) {
    auto& p = particles.particleBlock.particles[i];
    uint8_t flag = 0;
    const glm::vec3 pos =
      normalizedParticlePosition(p, particles.particleBlock.normalizedScale);
    if (glm::distance(pos, center) > radius) {
      flag = 1;
    }
    particles.flag_mask[i] = flag;
  }

  particles.particlesDirty = true;
}


void ClearVisibilityMask(ParticleArray& particles)
{
  for (size_t i = 0; i < particles.particleBlock.particles.size(); ++i) {
    particles.flag_mask[i] = 0;
  }
  particles.particlesDirty = true;
}
