//=== nanoflann_sph.h ===
#pragma once
#include <vector>
#include <nanoflann.hpp>
#include <glm/glm.hpp>
#include "analysis/isosurface/density_estimator.h"
#include <vector>

struct SimulationElementForKdTree {
    glm::vec3 pos;
    float     val;
};

struct PointCloud {
  std::vector<SimulationElementForKdTree>  particles;

  inline size_t kdtree_get_point_count() const { return particles.size(); }

  inline float kdtree_get_pt(const size_t idx, const size_t dim) const {
    if (dim == 0) return particles[idx].pos.x;
    if (dim == 1) return particles[idx].pos.y;
    return          particles[idx].pos.z;
  }


  template <class BBOX>
  bool kdtree_get_bbox(BBOX&) const { return false; }
};

using KDTree3D = nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<float, PointCloud>,
    PointCloud,
    3 /* dim */
>;


struct SPHInterpolator : public IDensityEstimator {
    SPHInterpolator(std::vector<SimulationElementForKdTree>&& pts);

    // Override the pure virtual function from IDensityEstimator.
    float sample(const glm::vec3& pos) const override;

private:
    // Implementation body: KNN plus kernel interpolation.
    float sampleValue(const glm::vec3& pos, size_t K, float alpha) const;

    PointCloud cloud_;
    KDTree3D   index_;
};
