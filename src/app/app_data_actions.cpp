#include "app/app_data_actions.h"

#include "data/simulation_dataset.h"
#include "app/state/normalization_config.h"

void NormalizeParticlePositions(SimulationDataset& particles,
                                NormalizationContext& normalization)
{
  particles.rescalePositions(normalization);
}
