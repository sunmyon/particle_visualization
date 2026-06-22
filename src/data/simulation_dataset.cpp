#include "data/simulation_dataset.h"
#include "data/header_info.h"
#include "core/PerfTimer.h"
#include "core/units.h"
#include "app/state/normalization_config.h"
#include "data/quantity_catalog_builder.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <utility>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {
using DatasetProfileClock = std::chrono::steady_clock;

double DatasetElapsedMs(DatasetProfileClock::time_point start)
{
  return std::chrono::duration<double, std::milli>(
           DatasetProfileClock::now() - start)
    .count();
}
}

void SimulationDataset::rescalePositions(NormalizationContext& ctx){
  simulationBlock.worldToRenderScale = ctx.toNormalizedScale();
  particlesDirty = true;  // Mark the global dirty flag.
};

bool SimulationDataset::setSimulationBlock(SimulationBlock&& newBlock, SimulationBlock* oldBlock, HeaderInfo& header, NormalizationContext& ctx, QuantityState& quantity) {
  TIME_FUNCTION();
  const auto totalStart = DatasetProfileClock::now();

  const auto moveStart = DatasetProfileClock::now();
  bool hadOld = !simulationBlock.particles.empty();
  if (oldBlock && hadOld) {
    *oldBlock = std::move(simulationBlock);
  }
  simulationBlock = std::move(newBlock);
  const double moveMs = DatasetElapsedMs(moveStart);

  const auto catalogStart = DatasetProfileClock::now();
  BuildQuantityCatalog(simulationBlock, quantity);
  const double catalogMs = DatasetElapsedMs(catalogStart);

  const auto rebuildStart = DatasetProfileClock::now();
  auto stats = simulationBlock.rebuild(ctx.desiredMax, quantity);
  const double rebuildMs = DatasetElapsedMs(rebuildStart);

  const auto rangeStart = DatasetProfileClock::now();
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
  const double rangeMs = DatasetElapsedMs(rangeStart);

  const auto unitsStart = DatasetProfileClock::now();
  if (header.flag_hdf5) {
    quantity.units.length_cm = header.UnitLength_in_cm;
    quantity.units.mass_g = header.UnitMass_in_g;
    quantity.units.velocity_cm_per_s = header.UnitVelocity_in_cm_per_s;
    quantity.units.hubble = header.HubbleParam;
    quantity.units.useComovingCoordinate = header.flag_comoving;
    quantity.units.updateDerived();
  }
  const double unitsMs = DatasetElapsedMs(unitsStart);

  const auto flagsStart = DatasetProfileClock::now();
  flag_mask.resize(simulationBlock.particles.size(), 0);
  flag_stress.assign(simulationBlock.particles.size(), 0);
  const double flagsMs = DatasetElapsedMs(flagsStart);
  
  simulationBlock_index = 0; // Or remove this later.
  particlesDirty = true;
  velocityDirty = true;
  flagParticleIndexDirty = true;

  std::fprintf(stderr,
               "[Dataset] setSimulationBlock n=%zu hadOld=%s move=%.3f ms "
               "catalog=%.3f ms rebuild=%.3f ms range=%.3f ms units=%.3f ms "
               "flags=%.3f ms total=%.3f ms\n",
               simulationBlock.particles.size(),
               hadOld ? "yes" : "no",
               moveMs,
               catalogMs,
               rebuildMs,
               rangeMs,
               unitsMs,
               flagsMs,
               DatasetElapsedMs(totalStart));

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
void SimulationDataset::computeStellarDensity(const std::array<bool,6>& selType,
					  bool flag_overwrite_hsml,
					  const NormalizationContext& ctx,
					  double time,
					  const UnitSystem& units)
{
  const auto totalStart = DatasetProfileClock::now();
  static constexpr size_t N_neighbours = 32;

  const bool flag_star = selType[3] || selType[4] || selType[5];

  std::vector<SimulationElement> & particles = simulationBlock.particles;
  
  // Extract only the selected particle types.
  std::vector<starParticle> filtered;
  filtered.reserve(particles.size());
  for (size_t i=0;i<particles.size();i++)
    {
      const SimulationElement& p = particles[i];

      const int t = (int)p.type;
      if (t < 0 || t >= 6) continue;
      if (!selType[t]) continue;

      struct starParticle sp;
      sp.type = p.type;
      renderPosition(p, simulationBlock.worldToRenderScale, sp.pos);
      sp.mass = p.mass;
      sp.index = i;
	
      filtered.push_back(sp);      
    }
  const double filterMs = DatasetElapsedMs(totalStart);

  // Copy into the data container. References or pointers can be used later if needed.
  StarParticleCloud cloud;
  cloud.particles = std::move(filtered);
    
  // Build the kd-tree.
  const auto treeStart = DatasetProfileClock::now();
  KDTreeType kdTree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10 /* max leaf */));
  kdTree.buildIndex();
  const double treeMs = DatasetElapsedMs(treeStart);

  double cosmofac = 1.;
  if(units.useComovingCoordinate)
    cosmofac = time;

  if(cosmofac < 1.e-2 || cosmofac > 1.)
    cosmofac = 1.;

  double hubble = units.hubble;
  if(hubble < 0.1 || hubble > 1.0)
    hubble = 1.;
  
  const double scale = ctx.toPhysicalScale();
  const double physicalLengthPc = scale * cosmofac * units.length_pc;
  const double physicalLengthCm = scale * cosmofac * units.length_cm;
  const double surfaceDensityFactor =
    units.mass_msun * hubble / std::max(physicalLengthPc * physicalLengthPc, 1.0e-300);
  const double volumeDensityFactor =
    units.mass_g * hubble * hubble /
    std::max(physicalLengthCm * physicalLengthCm * physicalLengthCm, 1.0e-300);
  const float invRenderScale =
    1.0f / std::max(simulationBlock.worldToRenderScale, 1.0e-30f);

  // Search neighbors for each particle.
  const auto computeStart = DatasetProfileClock::now();
  const std::ptrdiff_t nFiltered =
    static_cast<std::ptrdiff_t>(cloud.particles.size());

#pragma omp parallel for schedule(static) if(nFiltered > 1024)
  for (std::ptrdiff_t ii = 0; ii < nFiltered; ++ii) {
    const size_t i = static_cast<size_t>(ii);
    const auto& pi = cloud.particles[i];
    float query_pt[3] = {
      cloud.particles[i].pos[0],
      cloud.particles[i].pos[1],
      cloud.particles[i].pos[2]
    };

    std::array<KDTreeType::IndexType, N_neighbours> ret_indexes;
    std::array<float, N_neighbours> out_dists_sqr;
    size_t num_results = kdTree.knnSearch(&query_pt[0],
                                          N_neighbours,
                                          ret_indexes.data(),
                                          out_dists_sqr.data());
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
    
    int original_index = cloud.particles[i].index;
    if(flag_star)
      particles[original_index].density = totalMass * surfaceDensityFactor / area;
    else
      particles[original_index].density = density * volumeDensityFactor;

    if(flag_overwrite_hsml)
      particles[original_index].supportRadius = static_cast<float>(h) * invRenderScale;
  }
  const double computeMs = DatasetElapsedMs(computeStart);

  particlesDirty = true;
  std::fprintf(stderr,
               "[Dataset] computeStellarDensity selected=%zu filter=%.3f ms "
               "tree=%.3f ms compute=%.3f ms total=%.3f ms\n",
               cloud.particles.size(),
               filterMs,
               treeMs,
               computeMs,
               DatasetElapsedMs(totalStart));
}
