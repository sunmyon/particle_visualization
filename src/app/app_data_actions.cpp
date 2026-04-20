#include "app/app_data_actions.h"

#include "data/particle_array.h"
#include "app/normalization_config.h"

void NormalizeParticlePositions(ParticleArray& particles,
                                NormalizationContext& normalization)
{
  normalization.originalMax = particles.originalMax;
  particles.rescalePositions(normalization);
}
