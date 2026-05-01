#include <vector>
#include "data/spatial/particle_octree.h"
#include "analysis/isosurface/marching_cubes.h"
#include "analysis/isosurface/mesh_data.h"

struct IsoSurfaceParams {
  std::vector<ParticleDataForTree> particles;   ///< Input particle data, moved into the generator.
  BoundingBox           worldBox;    ///< Bounds of the full spatial domain.
  float                 isoLevel;    ///< Isosurface level.
  size_t                minParticles = 8;   ///< Minimum particle count threshold for octree subdivision.
  size_t                maxDepth     = 20;  ///< Maximum octree depth.
  int                   cornerReconstructionMode = 1; ///< 0=cell average, 1=shared corners, 2=face gradient.
  bool                  verbose      = false;
};

class IsoSurfaceGenerator {
  /// Marching-cubes implementation.
public:
  static Mesh generateMC(IsoSurfaceParams params);
};
