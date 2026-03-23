#include "render_actions.h"
#include "ui_state.h"

#include "main.h"
#include "object.h"
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

#ifdef STREAM_LINE
void UpdateSeedRegionPreview(StreamlineComputer& streamLine,
			     CubeManager& cubeManager,
			     RenderRuntimeState& render,
			     float seed_center[3],
			     float seed_len[3],
			     float seed_opacity)
{
  cubeManager.clearGroup("seedRegion");

  if (seed_len[0] > 0.f && seed_len[1] > 0.f && seed_len[2] > 0.f) {
    cubeManager.addCube(
      glm::vec3(seed_center[0], seed_center[1], seed_center[2]),
      glm::vec3(0.5f * seed_len[0], 0.5f * seed_len[1], 0.5f * seed_len[2]),
      glm::quat{1, 0, 0, 0},
      seed_opacity,
      "seedRegion"
    );
    streamLine.setRegionByHand(seed_center, seed_len);
  } else {
    streamLine.disableRegion();
  }

  render.flagCubesDirty = true;
}
#endif

#ifdef VOLUME_RENDERING
void PrepareVolumeRendering(ParticleArray& part,
                            lbvh::MortonBuilder& bvh,
			    VolumeRenderingRuntime& volume,
                            RenderRuntimeState& render)
{
  if (render.flagRT == 1) {
    volume.bvhResult = bvh.build(part.particleBlock.particles);
    lbvh::computeSigma(volume.bvhResult, volume.rho2sigma);
  }

  if (render.flagRT == 2) {
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

  render.showVolumeRendering = true;
  render.flagUpdateRendering = true;
}

void ReloadVolumeRendering(VolumeRenderingRuntime& volume, RenderRuntimeState& render)
{
  if (render.flagRT == 1) {
    lbvh::computeSigma(volume.bvhResult, volume.rho2sigma);
  }

  if (render.flagRT == 2) {
    buildIndexAndSigma(*volume.octTree.cpuTree, volume.rho2sigma, volume.octTree.order, volume.octTree.info, volume.octTree.toIdx);
  }

  render.showVolumeRendering = true;
  render.flagUpdateRendering = true;
}
#endif

#ifdef ISO_CONTOUR
void BuildIsoContourMesh(ParticleArray& part,
                         QuantityId selectedVar,
                         float isoLevel,
                         int max_treelevel,
			 IsoContourRuntime& iso,
                         RenderRuntimeState& render)
{
  render.showIsocontour = true;

  TrackingVector<ParticleDataForTree> particles;
  particles.reserve(part.particleBlock.particles.size());

  for (size_t ipart = 0; ipart < part.particleBlock.particles.size(); ++ipart) {
    const auto& pd = part.particleBlock.particles[ipart];
    float val = getScalarValue(part.particleBlock, pd, ipart, selectedVar);
    particles.push_back({glm::vec3(pd.pos[0], pd.pos[1], pd.pos[2]), val});
  }

  BoundingBox worldBox;
  worldBox.min = glm::vec3(FLT_MAX);
  worldBox.max = glm::vec3(-FLT_MAX);
  for (const auto& p : particles) {
    worldBox.min = glm::min(worldBox.min, p.pos);
    worldBox.max = glm::max(worldBox.max, p.pos);
  }

  IsoSurfaceParams params;
  params.particles = std::move(particles);
  params.worldBox = worldBox;
  params.isoLevel = isoLevel;
  params.minParticles = 8;
  params.maxDepth = max_treelevel;

  auto mesh = IsoSurfaceGenerator::generateVTK(std::move(params));

  iso.verts = mesh.vertices;
  iso.inds  = mesh.indices;
}
#endif
