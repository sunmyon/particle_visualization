#include "IsoSurface/iso_contour_build.h"

#ifdef ISO_CONTOUR
#include <cfloat>
#include <glm/glm.hpp>

#include "data/particle_array.h"
#include "core/quantity.h"
#include "IsoSurface/IsoSurfaceGenerator.h"
#include "app/state/app_state.h"

void BuildIsoContourGeometry(ParticleArray& part,
                             QuantityId selectedVar,
                             float isoLevel,
                             int max_treelevel,
                             IsoContourGeometryState& iso)
{
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

  iso.verts = std::move(mesh.vertices);
  iso.inds  = std::move(mesh.indices);
}

#endif
