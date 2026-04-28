#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <unordered_map>
#include <cstring>
#include "data/spatial/particle_octree.h"
#include "analysis/isosurface/mesh_data.h"

class IsoSurfaceTreeField;

class MarchingCubes {
public:
  struct Edge {
    glm::vec3 p0, p1;
    Edge(const glm::vec3& a, const glm::vec3& b) {
      if (a.x<b.x || (a.x==b.x && (a.y<b.y || (a.y==b.y && a.z<=b.z)))) {
	p0=a; p1=b;
      } else {
	p0=b; p1=a;
      }
    }
    bool operator==(Edge const& o) const {
      return p0==o.p0 && p1==o.p1;
    }
  };

  struct EdgeHash {
    size_t operator()(Edge const& e) const noexcept {
      auto hf = [](float v){
	uint32_t x; static_assert(sizeof(x)==sizeof(v),"");
	std::memcpy(&x,&v,sizeof(v));
	return size_t(x);
      };
      size_t h0 = hf(e.p0.x) ^ (hf(e.p0.y)<<1) ^ (hf(e.p0.z)<<2);
      size_t h1 = hf(e.p1.x) ^ (hf(e.p1.y)<<1) ^ (hf(e.p1.z)<<2);
      return h0 ^ (h1<<1);
    }
  };

  struct GridKey {
    int64_t x,y,z;
    bool operator==(GridKey const& o) const noexcept {
      return x==o.x && y==o.y && z==o.z;
    }
  };
  
  struct GridKeyHash {
    size_t operator()(GridKey const& k) const noexcept {
      // Simple 3D hash combination.
      return k.x*73856093ull
	^ k.y*19349663ull
	^ k.z*83492791ull;
    }
  };
  
  
  // Quantization step used to absorb small numerical errors.
  static constexpr float EPS_POS = 1e-6f;

  static GridKey quantizePosition(const glm::vec3 &p) {
    // GLSL has no llroundf, so use llround from <cmath>.
    return GridKey {
      llround(p.x / EPS_POS),
      llround(p.y / EPS_POS),
      llround(p.z / EPS_POS)
    };
  }
  
  // leaves: leaf nodes from the spatially subdivided octree.
  // particles: pointers and ranges into the source data.
  // voxelSize and isoLevel: generation parameters.
  static Mesh buildIsoSurface(const TrackingVector<const ParticleOctree::Node*>& leaves,
                              const IsoSurfaceTreeField& field,
			      float isoLevel);

  static Mesh buildAndStitchIsoSurface(const TrackingVector<const ParticleOctree::Node*>& leaves,
                                       const IsoSurfaceTreeField& field,
                                       const ParticleOctree& tree,
                                       float isoLevel);

  
  static void stitchFace(const ParticleOctree& tree,
			 const ParticleOctree::Node*  coarse,
			 const ParticleOctree::Node*  fine,
			 int                  dir,
			 Mesh&                out);  

  static const std::array<glm::vec3,8> cubeOffsets;
  
private:
  static glm::vec3 vertexInterp(float iso, const glm::vec3& p1, const glm::vec3& p2,
				float v1, float v2);
  
  static const int  edgeTable[256];
  static const int  triTable[256][16];
  static const int edgeToVertex[12][2];

  static std::vector<Edge> getFaceEdges(const BoundingBox& box, int dir);
  static TrackingVector<Edge>
  findAllFineEdges(const ParticleOctree&      tree,
		   const ParticleOctree::Node* coarse,
		   int                        dir,
		   const Edge&                cedge);
  
  static std::unordered_map<Edge, unsigned, EdgeHash> globalEdgeMap;

  // Map from coordinate key to vertex index.
  static std::unordered_map<GridKey, unsigned, GridKeyHash> vertexMap;
};
