// EllipseFitter.h
#ifndef ELLIPSE_FITTER_H
#define ELLIPSE_FITTER_H

#include <vector>
#include <queue>
#include <cmath>
#include <limits>
#include <algorithm>
#include <Eigen/Dense>
#include <nanoflann.hpp>

#include "main.h"
#include <glm/glm.hpp>

struct Ellipsoid {
  double a{0}, b{0}, c{0};          // 半長軸・半中軸・半短軸
  Eigen::Vector3d center{0,0,0};    // 質量中心
  Eigen::Matrix3d axes = Eigen::Matrix3d::Identity(); // 列ベクトルが主軸 (右手系)
};


class EllipseFitter {
  public:
  EllipseFitter() = default;
  
  /**
   * data から indexA, indexB 間の最小等密度面を抽出し楕円を返す
   * - data 参照はコピー不要
   * - 毎回内部で KD-tree を構築
   */
  void computeEllipse(const TrackingVector<ParticleData>& data, int ID_A, int ID_B);
  glm::mat4 getModelMatrix(void) const;
  void getEllipsoids(double *a, double *b, double *c, double *n);
  
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

  Ellipsoid ellipsoid_;
  double density_threshold_;
  
  static constexpr int maxIterForLowestBound = 10;
  static constexpr int bisectIters=100;
  static constexpr float tol = 1.e-3;
};

#endif // ELLIPSE_FITTER_H
