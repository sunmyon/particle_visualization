#include <vector>
#include "data/spatial/particle_octree.h"
#include "analysis/isosurface/marching_cubes.h"
#include "analysis/isosurface/connectivity_test.h"
#include "analysis/isosurface/iso_surface_field.h"
#include "analysis/isosurface/mesh_data.h"

struct IsoSurfaceParams {
  TrackingVector<ParticleDataForTree> particles;   ///< Input particle data, moved into the generator.
  BoundingBox           worldBox;    ///< Bounds of the full spatial domain.
  float                 isoLevel;    ///< Isosurface level.
  size_t                minParticles = 8;   ///< Minimum particle count threshold for octree subdivision.
  size_t                maxDepth     = 20;  ///< Maximum octree depth.
  bool                  verbose      = false;
};

class IsoSurfaceGenerator {
  /// Marching-cubes implementation.
public:
  static Mesh generateMC(IsoSurfaceParams params) {
    ParticleOctree octree(
      std::move(params.particles),
      params.worldBox,
      params.minParticles,
      params.maxDepth,
      params.isoLevel
    );
    octree.balanceTree(true);

    IsoSurfaceTreeField field = BuildIsoSurfaceTreeField(octree);
    TrackingVector<const ParticleOctree::Node*> leaves =
      field.leavesCrossing(params.isoLevel);

    auto mesh = MarchingCubes::buildAndStitchIsoSurface(leaves,
                                                        field,
                                                        octree,
                                                        params.isoLevel);
    if (params.verbose) {
      runConnectivityQuickCheck(octree, leaves, mesh);
    }

    return mesh;
  }

  static Mesh generateVTK(IsoSurfaceParams params);
};
