// EllipseFitter.h
#ifndef ELLIPSE_FITTER_H
#define ELLIPSE_FITTER_H

#include <vector>
#include <Eigen/Dense>
#include <memory>

#include <nanoflann.hpp>

#include "main.h"
#include "object.h" 
#include <glm/glm.hpp>

class EllipseFitter {
  public:
  EllipseFitter() = default;
  
  /**
   * data から indexA, indexB 間の最小等密度面を抽出し楕円を返す
   * - data 参照はコピー不要
   * - 毎回内部で KD-tree を構築
   */
  bool computeEllipse(const TrackingVector<ParticleData>& data, int ID_A, int ID_B, EllipsoidObject& out);
  double getDensityThreshold() {return density_threshold_;};
  
 private:
  template<typename T>
  struct PointCloud {
    const TrackingVector<T>* pts;
    inline size_t kdtree_get_point_count() const { return pts->size(); }
    inline double kdtree_get_pt(const size_t idx, const size_t dim) const {
      return (*pts)[idx].pos[dim];
    }
    template <class BBOX> bool kdtree_get_bbox(BBOX&) const { return false; }
  };
  
  // ParticleData 用 KD-tree の型エイリアス
  using KDTree = nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<float, PointCloud<ParticleData>>,
    PointCloud<ParticleData>,
    3
    >;
  
  // 毎回 new/delete する
  std::unique_ptr<KDTree> kdtree_;
  const TrackingVector<ParticleData>* dataPtr_{nullptr};
  
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
