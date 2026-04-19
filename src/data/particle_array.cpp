#include "data/particle_array.h"
#include "core/PerfTimer.h"

bool ParticleArray::setParticleBlock(ParticleBlock&& newBlock, ParticleBlock* oldBlock) {
  TIME_FUNCTION();

  bool hadOld = !particleBlock.particles.empty();
  if (oldBlock && hadOld) {
    *oldBlock = std::move(particleBlock);
  }
  particleBlock = std::move(newBlock);

  auto stats = particleBlock.rebuild(desiredMax);

  for (int q = 0; q < particleBlock.nUIQ; ++q) {
    for (int t = 0; t < kNumTypes; ++t) {
      particleValueMin[q][t] = stats.valueMin[q][t];
      particleValueMax[q][t] = stats.valueMax[q][t];
    }
  }

  originalMax = stats.originalMax;

  if (particleBlock.header.flag_hdf5) {
    units.length_cm = particleBlock.header.UnitLength_in_cm;
    units.mass_g = particleBlock.header.UnitMass_in_g;
    units.velocity_cm_per_s = particleBlock.header.UnitVelocity_in_cm_per_s;
    units.hubble = particleBlock.header.HubbleParam;
    units.useComovingCoordinate = particleBlock.header.flag_comoving;
    units.updateDerived();
  }

  flag_mask.resize(particleBlock.particles.size(), 0);
  
  particleBlock_index = 0; // あるいは廃止
  particlesDirty = true;
  flagParticleIndexDirty = true;

  return hadOld;
}

#include <nanoflann.hpp>

namespace{
  // 星粒子の構造体
  struct starParticle {
    float pos[3];
    double mass;
    int type;
    int index;
    double density; // 密度を格納するフィールド
    // 他のメンバ...
  };

  // nanoflann用のデータコンテナ
  struct StarParticleCloud {
    TrackingVector<starParticle> particles;

    // kd-tree インターフェース
    inline size_t kdtree_get_point_count() const { return particles.size(); }
    
    // 指定インデックスの次元 dim の値を返す
    inline float kdtree_get_pt(const size_t idx, const size_t dim) const {
      return particles[idx].pos[dim];
    }
    
    // バウンディングボックスは省略（falseを返す）
    template <class BBOX>
    bool kdtree_get_bbox(BBOX & /*bb*/) const { return false; }
  };

  // kd-treeの型定義（3次元用）
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

  // 各星粒子について、探索半径 searchRadius 内の全粒子の質量を合計し、
  // 面積 (π * searchRadius²) で割ることで密度 (Msun/pc²) を計算する関数
void ParticleArray::computeStellarDensity(const std::array<bool,6>& selType, bool flag_overwrite_hsml)
{
  const int N_neighbours = 32;

  bool flag_star = false;
  if(selType[3] == true || selType[4] == true || selType[5] == true)
    flag_star = true;

  TrackingVector<ParticleData> & particles = particleBlock.particles;
  
  // type >= 3 の粒子のみを抽出
  TrackingVector<starParticle> filtered;
  for (size_t i=0;i<particles.size();i++)
    {
      const ParticleData& p = particles[i];

      const int t = (int)p.type;
      if (t < 0 || t >= 6) continue;
      if (!selType[t]) continue;

      struct starParticle sp;
      sp.type = p.type;
      sp.pos[0] = p.pos[0];
      sp.pos[1] = p.pos[1];
      sp.pos[2] = p.pos[2];
      sp.mass = p.mass;
      sp.index = i;
	
      filtered.push_back(sp);      
    }
  
  TrackingVector<double> densities(filtered.size(), 0.0);

  // データコンテナにコピー（必要に応じて参照やポインタを使ってもよい）
  StarParticleCloud cloud;
  cloud.particles = filtered;
    
  // kd-treeの構築
  KDTreeType kdTree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10 /* max leaf */));
  kdTree.buildIndex();

  // knnSearch の結果を格納するためのコンテナ
  TrackingVector<KDTreeType::IndexType> ret_indexes(N_neighbours);
  TrackingVector<float> out_dists_sqr(N_neighbours);

  double cosmofac = 1.;
  if(units.useComovingCoordinate)
    cosmofac = particleBlock.header.time;

  if(cosmofac < 1.e-2 || cosmofac > 1.)
    cosmofac = 1.;

  double hubble = units.hubble;
  if(hubble < 0.1 || hubble > 1.0)
    hubble = 1.;
  
  nanoflann::SearchParameters params;
  // 各粒子について近傍を探索
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
        
    // 面積 = π * r^2
    double area = M_PI * h * h;
    
    int original_index = cloud.particles[i].index;
    if(flag_star)
      particles[original_index].density = totalMass * units.mass_msun
	/ area / std::pow(originalMax / desiredMax * cosmofac * units.length_pc, 2.) * units.hubble;
    else
      particles[original_index].density = density * units.mass_g / std::pow(originalMax / desiredMax * cosmofac * units.length_cm, 3.) * units.hubble * units.hubble;

    if(flag_overwrite_hsml)
      particles[original_index].Hsml = h;
    
    printf("i=%d mass=%g h=%g desnity=%g %g cosmofac=%g scale_len=%g hubble=%g\n"
	   , original_index, totalMass, h, particles[original_index].density, density, cosmofac, originalMax / desiredMax * cosmofac * units.length_cm, units.hubble);
  }
}

