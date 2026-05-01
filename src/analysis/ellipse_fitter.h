#ifndef ELLIPSE_FITTER_H
#define ELLIPSE_FITTER_H

#include <vector>
#include <cstdint>
#include <Eigen/Dense>
#include <memory>

#include <nanoflann.hpp>

#include "render/scene_objects.h"
#include "data/sample_coordinates.h"
#include "data/simulation_block.h"
#include "data/simulation_element.h"
#include <glm/glm.hpp>

class EllipseFitter {
  public:
  EllipseFitter() = default;
  
  /**
   * Extract the lowest isodensity surface between indexA and indexB and return an ellipsoid.
   * - data is passed by reference and does not need copying.
   * - A KD-tree is built internally on each call.
   */
  bool computeEllipse(const SimulationBlock& block,
                      int64_t ID_A,
                      int64_t ID_B,
                      EllipsoidObject& out);
  double getDensityThreshold() {return density_threshold_;};
  
 private:
  template<typename T>
  struct PointCloud {
    const std::vector<T>* pts;
    float worldToRenderScale = 1.0f;
    inline size_t kdtree_get_point_count() const { return pts->size(); }
    inline double kdtree_get_pt(const size_t idx, const size_t dim) const {
      return renderPosition((*pts)[idx], worldToRenderScale)[dim];
    }
    template <class BBOX> bool kdtree_get_bbox(BBOX&) const { return false; }
  };
  
  // KD-tree type alias for SimulationElement.
  using KDTree = nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<float, PointCloud<SimulationElement>>,
    PointCloud<SimulationElement>,
    3
    >;
  
  // Recreated for each run.
  std::unique_ptr<KDTree> kdtree_;
  const std::vector<SimulationElement>* dataPtr_{nullptr};
  float worldToRenderScale_ = 1.0f;
  
  std::vector<int> extractComponent(double thr, int seedIndex) const;
  void computePCA3D(const std::vector<int>& comp,
		    Eigen::Matrix3d& axes,
		    Eigen::Vector3d& centroid,
		    Eigen::Vector3d& evals) const;
  
  int find_nearest_gas_particle(const float pos[3]) const;

  double density_threshold_ = 0.;
  
  static constexpr int maxIterForLowestBound = 10;
  static constexpr int bisectIters=100;
  static constexpr float tol = 1.e-3;
};

#endif // ELLIPSE_FITTER_H
