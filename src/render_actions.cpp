#include "render_actions.h"
#include "app/runtime_state.h"

#include "object.h"
#include "data/particle_array.h"

#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

#ifdef ISO_CONTOUR
#include "IsoSurface/IsoSurfaceGenerator.h"
#endif

#ifdef STREAM_LINE
#include "StreamLine/stream_line_new.h"
#endif

#ifdef VOLUME_RENDERING
#include "BVH/BVH.hpp"
#include "VolumeRendering/tau_sph.h"
#include "VolumeRendering/TransferFunctionEditor.hpp"
#include "VolumeRendering/OpacityComputer.hpp"
#endif

#ifdef VOLUME_RENDERING
void PrepareVolumeRendering(ParticleArray& part,
                            lbvh::MortonBuilder& bvh,
			    VolumeRenderingRuntime& volume,
                            RenderRuntimeState& render)
{
  if (render.volume.flagRT == 1) {
    volume.bvhResult = bvh.build(part.particleBlock.particles);
    lbvh::computeSigma(volume.bvhResult, volume.rho2sigma);
  }

  if (render.volume.flagRT == 2) {
    TrackingVector<ParticleDataForTree> particles;
    particles.reserve(part.particleBlock.particles.size());

    for (size_t ipart = 0; ipart < part.particleBlock.particles.size(); ++ipart) {
      const auto& pd = part.particleBlock.particles[ipart];
      if (pd.type != 0) continue;
      particles.push_back({glm::vec3(pd.pos[0], pd.pos[1], pd.pos[2]), pd.density});
    }

    BoundingBox worldBox;
    worldBox.min = glm::vec3(FLT_MAX);
    worldBox.max = glm::vec3(-FLT_MAX);
    for (const auto& p : particles) {
      worldBox.min = glm::min(worldBox.min, p.pos);
      worldBox.max = glm::max(worldBox.max, p.pos);
    }

    volume.octTree.cpuTree = std::make_unique<ParticleOctree>(std::move(particles), worldBox, 4, 20);
    buildIndexAndSigma(*volume.octTree.cpuTree, volume.rho2sigma, volume.octTree.order, volume.octTree.info, volume.octTree.toIdx);
  }

  render.volume.show = true;
  render.volume.cpuUpdated = true;
}

void ReloadVolumeRendering(VolumeRenderingRuntime& volume, RenderRuntimeState& render)
{
  if (render.volume.flagRT == 1) {
    lbvh::computeSigma(volume.bvhResult, volume.rho2sigma);
  }

  if (render.volume.flagRT == 2) {
    buildIndexAndSigma(*volume.octTree.cpuTree, volume.rho2sigma, volume.octTree.order, volume.octTree.info, volume.octTree.toIdx);
  }

  render.volume.show = true;
  render.volume.cpuUpdated = true;
}
#endif
