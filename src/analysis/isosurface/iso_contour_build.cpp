#include "analysis/isosurface/iso_contour_build.h"

#ifdef ISO_CONTOUR
#include <cfloat>
#include <algorithm>
#include <glm/glm.hpp>

#include "analysis/isosurface/isosurface_generator.h"
#include "core/quantity.h"
#include "data/particle_block.h"

Mesh BuildIsoContourMesh(const ParticleBlock& block,
                         const IsoContourBuildParams& build)
{
  TrackingVector<ParticleDataForTree> particles;
  particles.reserve(block.particles.size());

  for (size_t ipart = 0; ipart < block.particles.size(); ++ipart) {
    const auto& pd = block.particles[ipart];
    float val = getScalarValue(block, pd, ipart, build.selectedQuantity);
    particles.push_back({glm::vec3(pd.pos[0], pd.pos[1], pd.pos[2]), val});
  }

  if (particles.empty()) {
    return {};
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
  params.isoLevel = build.isoLevel;
  params.minParticles = build.minParticles;
  params.maxDepth = static_cast<size_t>(std::max(1, build.maxTreeLevel));
  params.verbose = build.verbose;

  if (build.useVTK) {
    return IsoSurfaceGenerator::generateVTK(std::move(params));
  }
  return IsoSurfaceGenerator::generateMC(std::move(params));
}

#endif
