#include "app/app_visibility_actions.h"
#include "app/state/view_filter_config.h"

#include "data/simulation_dataset.h"
#include "data/sample_coordinates.h"
#include "interaction/camera.h"

void ApplyCullingSphere(SimulationDataset& particles,
                        const ViewFilterConfig& viewFilter)
{
  const float worldToRender =
    particles.simulationBlock.worldToRenderScale > 0.0f
      ? particles.simulationBlock.worldToRenderScale
      : 1.0f;
  const double radius = viewFilter.radiusCullingSphere * worldToRender;
  const glm::vec3 center = viewFilter.center * worldToRender;
  
  for (size_t i = 0; i < particles.simulationBlock.particles.size(); ++i) {
    auto& p = particles.simulationBlock.particles[i];
    uint8_t flag = 0;
    const glm::vec3 pos =
      renderPosition(p, particles.simulationBlock.worldToRenderScale);
    if (glm::distance(pos, center) > radius) {
      flag = 1;
    }
    particles.flag_mask[i] = flag;
  }

  particles.particlesDirty = true;
}


void ClearVisibilityMask(SimulationDataset& particles)
{
  for (size_t i = 0; i < particles.simulationBlock.particles.size(); ++i) {
    particles.flag_mask[i] = 0;
  }
  particles.particlesDirty = true;
}
