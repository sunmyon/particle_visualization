#include "app/app_data_actions.h"

#include "data/particle_array.h"
#include "app/state/normalization_config.h"

void NormalizeParticlePositions(ParticleArray& particles,
                                NormalizationContext& normalization)
{
  particles.rescalePositions(normalization);
}
