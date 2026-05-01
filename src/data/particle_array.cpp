#include "data/particle_array.h"
#include "data/header_info.h"
#include "core/PerfTimer.h"
#include "core/units.h"
#include "app/state/normalization_config.h"
#include "data/quantity_catalog_builder.h"

void ParticleArray::rescalePositions(NormalizationContext& ctx){
  particleBlock.normalizedScale = ctx.toNormalizedScale();
  particlesDirty = true;  // Mark the global dirty flag.
};

bool ParticleArray::setParticleBlock(ParticleBlock&& newBlock, ParticleBlock* oldBlock, HeaderInfo& header, NormalizationContext& ctx, QuantityState& quantity) {
  TIME_FUNCTION();

  bool hadOld = !particleBlock.particles.empty();
  if (oldBlock && hadOld) {
    *oldBlock = std::move(particleBlock);
  }
  particleBlock = std::move(newBlock);

  BuildQuantityCatalog(particleBlock, quantity.catalog);
  auto stats = particleBlock.rebuild(ctx.desiredMax, quantity.catalog);

  for (int q = 0; q < kMaxQ; ++q) {
    for (int t = 0; t < kNumTypes; ++t) {
      quantity.range.valueMin[q][t] = 0.0f;
      quantity.range.valueMax[q][t] = 0.0f;
    }
  }

  for (int q = 0; q < quantity.catalog.nUIQ; ++q) {
    const int qidx = static_cast<int>(quantity.catalog.uiQ[q]);
    if (qidx < 0 || qidx >= kMaxQ) {
      continue;
    }
    for (int t = 0; t < kNumTypes; ++t) {
      quantity.range.valueMin[qidx][t] = stats.valueMin[q][t];
      quantity.range.valueMax[qidx][t] = stats.valueMax[q][t];
    }
  }

  quantity.range.originalMax = stats.originalMax;
  ctx.originalMax = stats.originalMax;

  if (header.flag_hdf5) {
    quantity.units.length_cm = header.UnitLength_in_cm;
    quantity.units.mass_g = header.UnitMass_in_g;
    quantity.units.velocity_cm_per_s = header.UnitVelocity_in_cm_per_s;
    quantity.units.hubble = header.HubbleParam;
    quantity.units.useComovingCoordinate = header.flag_comoving;
    quantity.units.updateDerived();
  }

  flag_mask.resize(particleBlock.particles.size(), 0);
  flag_stress.assign(particleBlock.particles.size(), 0);
  
  particleBlock_index = 0; // Or remove this later.
  particlesDirty = true;
  flagParticleIndexDirty = true;

  return hadOld;
}

#include <nanoflann.hpp>

namespace{
  // Star particle record.
  struct starParticle {
    float pos[3];
    double mass;
    int type;
    int index;
    double density; // Density field.
    // Other members can be added here.
  };

  // Data container for nanoflann.
  struct StarParticleCloud {
    std::vector<starParticle> particles;

    // kd-tree interface.
    inline size_t kdtree_get_point_count() const { return particles.size(); }
    
    // Return the coordinate value for dimension dim at the specified index.
    inline float kdtree_get_pt(const size_t idx, const size_t dim) const {
      return particles[idx].pos[dim];
    }
    
    // Omit the bounding box by returning false.
    template <class BBOX>
    bool kdtree_get_bbox(BBOX & /*bb*/) const { return false; }
  };

  // kd-tree type definition for 3D points.
  typedef nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<float, StarParticleCloud>,
    StarParticleCloud,
    3 /* dim */
    > KDTreeType;
}

static inline double cubic_spline_W(double r, double h) {
  const double q = r / h;
  const double sigma = 1.0 / (M_PI * h * h * h);
  if (q < 1.0) {
    return sigma * (1.0 - 1.5 * q * q + 0.75 * q * q * q);
  } else if (q < 2.0) {
    const double term = 2.0 - q;
    return sigma * (0.25 * term * term * term);
  } else {
    return 0.0;
  }
}

// For each star particle, sum all particle masses within the search radius
// and divide by area, pi * searchRadius^2, to compute density in Msun/pc^2.
void ParticleArray::computeStellarDensity(const std::array<bool,6>& selType,
					  bool flag_overwrite_hsml,
					  const NormalizationContext& ctx,
					  double time,
					  const UnitSystem& units)
{
  const int N_neighbours = 32;

  bool flag_star = false;
  if(selType[3] == true || selType[4] == true || selType[5] == true)
    flag_star = true;

  std::vector<ParticleData> & particles = particleBlock.particles;
  
  // Extract only the selected particle types.
  std::vector<starParticle> filtered;
  for (size_t i=0;i<particles.size();i++)
    {
      const ParticleData& p = particles[i];

      const int t = (int)p.type;
      if (t < 0 || t >= 6) continue;
      if (!selType[t]) continue;

      struct starParticle sp;
      sp.type = p.type;
      normalizedParticlePosition(p, particleBlock.normalizedScale, sp.pos);
      sp.mass = p.mass;
      sp.index = i;
	
      filtered.push_back(sp);      
    }
  
  std::vector<double> densities(filtered.size(), 0.0);

  // Copy into the data container. References or pointers can be used later if needed.
  StarParticleCloud cloud;
  cloud.particles = filtered;
    
  // Build the kd-tree.
  KDTreeType kdTree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10 /* max leaf */));
  kdTree.buildIndex();

  // Containers for knnSearch results.
  std::vector<KDTreeType::IndexType> ret_indexes(N_neighbours);
  std::vector<float> out_dists_sqr(N_neighbours);

  double cosmofac = 1.;
  if(units.useComovingCoordinate)
    cosmofac = time;

  if(cosmofac < 1.e-2 || cosmofac > 1.)
    cosmofac = 1.;

  double hubble = units.hubble;
  if(hubble < 0.1 || hubble > 1.0)
    hubble = 1.;
  
  nanoflann::SearchParameters params;
  // Search neighbors for each particle.
  for (size_t i = 0; i < cloud.particles.size(); i++) {
    const auto& pi = cloud.particles[i];
    float query_pt[3] = {
      cloud.particles[i].pos[0],
      cloud.particles[i].pos[1],
      cloud.particles[i].pos[2]
    };

    size_t num_results = kdTree.knnSearch(&query_pt[0], N_neighbours, ret_indexes.data(), out_dists_sqr.data());    
    if (num_results == 0)
      continue;

    double h = std::sqrt(out_dists_sqr[num_results - 1]);
    if (h <= 0) continue;

    double totalMass = 0., density = 0.;

    for (size_t j = 0; j < num_results; j++)
      {
	size_t idx = ret_indexes[j];
	
	const auto& pj = cloud.particles[idx];

	double dx = pi.pos[0] - pj.pos[0];
	double dy = pi.pos[1] - pj.pos[1];
	double dz = pi.pos[2] - pj.pos[2];
	double r = std::sqrt(dx*dx + dy*dy + dz*dz);
	double m = pj.mass;

	totalMass += m;
	density += m * cubic_spline_W(r, h);	    	  
      }
        
    // Area = pi * r^2.
    double area = M_PI * h * h;
    double scale = ctx.toPhysicalScale();
    
    int original_index = cloud.particles[i].index;
    if(flag_star)
      particles[original_index].density = totalMass * units.mass_msun
	/ area / std::pow(scale * cosmofac * units.length_pc, 2.) * units.hubble;
    else
      particles[original_index].density = density * units.mass_g / std::pow(scale * cosmofac * units.length_cm, 3.) * units.hubble * units.hubble;

    if(flag_overwrite_hsml)
      particles[original_index].original_hsml =
        h / std::max(particleBlock.normalizedScale, 1.0e-30f);
    
    printf("i=%d mass=%g h=%g desnity=%g %g cosmofac=%g scale_len=%g hubble=%g\n"
	   , original_index, totalMass, h, particles[original_index].density, density, cosmofac, scale * cosmofac * units.length_cm, units.hubble);
  }
}
