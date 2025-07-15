#include <vector>
#include "IsoSurface/ParticleOctree.h"
#include "IsoSurface/marching_cubes.h"
#include "IsoSurface/connectivity_test.h"
#include "IsoSurface/mesh_data.h"

struct IsoSurfaceParams {
  TrackingVector<ParticleDataForTree> particles;   ///< 入力粒子データ（ムーブして渡す）
  BoundingBox           worldBox;    ///< 空間全体の境界
  float                 isoLevel;    ///< 等値面レベル
  size_t                minParticles = 8;   ///< Octree 分割の最小粒子数閾値
  size_t                maxDepth     = 20;  ///< Octree の最大深さ
};

class IsoSurfaceGenerator {
  /// marching-cubes 版
public:
  static Mesh generateMC(IsoSurfaceParams params) {
    ParticleOctree octree(
      std::move(params.particles),
      params.worldBox,
      params.isoLevel,
      params.minParticles,
      params.maxDepth
    );

    auto rawLeaves = octree.getAllLeafNodes();
    TrackingVector<const ParticleOctree::Node*> leaves;
    leaves.reserve(rawLeaves.size());
    for (auto* n : rawLeaves) leaves.push_back(n);

    auto mesh = MarchingCubes::buildAndStitchIsoSurface(leaves, octree, params.isoLevel);
    runConnectivityQuickCheck(octree, mesh, params.isoLevel);

    return mesh;
  }

  static Mesh generateVTK(IsoSurfaceParams params);
};
