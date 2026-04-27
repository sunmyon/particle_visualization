#include "app/app_visibility_actions.h"
#include "app/state/view_filter_config.h"
#include "app/state/normalization_config.h"

#include "data/particle_array.h"
#include "interaction/camera.h"

void ApplyCullingSphere(ParticleArray& particles,
			const NormalizationContext& normalization,
                        const ViewFilterConfig& viewFilter)
{
  const double scaleRad = (viewFilter.radiusIsOriginal ? normalization.toNormalizedScale(): 1.);
  const double scalePos = (viewFilter.centerIsOriginal ? normalization.toNormalizedScale(): 1.);

  const double radius = viewFilter.radiusCullingSphere * scaleRad;
  const glm::vec3 center = viewFilter.center * static_cast<float>(scalePos);
  
  for (size_t i = 0; i < particles.particleBlock.particles.size(); ++i) {
    auto& p = particles.particleBlock.particles[i];
    uint8_t flag = 0;
    if (glm::distance(glm::vec3(p.pos[0], p.pos[1], p.pos[2]), center) > radius) {
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
