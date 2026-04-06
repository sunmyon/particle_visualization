#ifdef GEOMETRICAL_ANALYSIS
// EllipseFitter.cpp
#include <queue>
#include "GeometricAnalysis/ellipse_fitter.h"
#include "data/particle_data.h"

#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp> 

std::vector<int> EllipseFitter::extractComponent(double thr, int seedIndex) const {
    std::vector<bool> visited(dataPtr_->size(), false);
    std::vector<int> comp;
    if ((*dataPtr_)[seedIndex].density < thr) return comp;

    std::queue<int> Q;
    Q.push(seedIndex);
    visited[seedIndex] = true;

    while (!Q.empty()) {
        int i = Q.front(); Q.pop();
        comp.push_back(i);

	// KDTree_t に依存する型エイリアスの取得
	typedef typename KDTree::IndexType  IndexType;
	typedef typename KDTree::DistanceType DistanceType;
	typedef nanoflann::ResultItem<IndexType, DistanceType> MyResultItem;
	
	std::vector<MyResultItem> ret;	
        nanoflann::SearchParameters sp;
	float q[3] = {(*dataPtr_)[i].pos[0],(*dataPtr_)[i].pos[1],(*dataPtr_)[i].pos[2]};
        kdtree_->radiusSearch(q, (*dataPtr_)[i].Hsml * (*dataPtr_)[i].Hsml, ret, sp);
        for (auto& r : ret) {
            int j = r.first;
            if (!visited[j] && (*dataPtr_)[j].density >= thr) {
                visited[j] = true;
                Q.push(j);
            }
        }
    }
    return comp;
}

int EllipseFitter::find_nearest_gas_particle(const float pos[3]) const {
  size_t N = 4;
  
  while(1){
    // kNN で全点ソート
    std::vector<size_t>   indices(N);
    std::vector<double>   dists2(N);
    nanoflann::KNNResultSet<double> resultSet(N);
    resultSet.init(indices.data(), dists2.data());
    kdtree_->findNeighbors(resultSet, pos, nanoflann::SearchParameters());

    // 距離順に巡って type==0 のものを返す
    for (size_t i = 0; i < N; ++i) {
      int idx = static_cast<int>(indices[i]);
      if ((*dataPtr_)[idx].type == 0) {
	return idx;
      }
    }

    N *= 2;
  }
  
  return -1;    
}


bool EllipseFitter::computeEllipse(const TrackingVector<ParticleData>& data, int ID_A, int ID_B, EllipsoidObject& out) 
{
  dataPtr_ = &data;

  int index_a, index_b;  
  size_t count = 0;
  for(size_t i=0;i<data.size();i++){
    if((*dataPtr_)[i].ID == ID_A){
      index_a = i;
      count++;	
    }

    if((*dataPtr_)[i].ID == ID_B){
      index_b = i;
      count++;
    }

    if(count == 2)
      break;
  }

  if(count != 2){
    printf("Could not find two specified particles with ID=%d and %d (count_found=%zu < 2)\n", ID_A, ID_B, count );
    return false;
  }

  // 2) KD-tree を動的に構築
  PointCloud<ParticleData> cloud{ &data };
  kdtree_.reset(new KDTree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams()));
  kdtree_->buildIndex();
  
  int indexA = find_nearest_gas_particle((*dataPtr_)[index_a].pos);
  int indexB = find_nearest_gas_particle((*dataPtr_)[index_b].pos);

  double rhoA = (*dataPtr_)[indexA].density;
  double rhoB = (*dataPtr_)[indexB].density;

  double high = std::min(rhoA, rhoB);
  double low = 0.1 * high;

  if(high < 1.e3){
    density_threshold_ = 0.;
    out.clear();
    return false;    
  }
  
  for (int it = 0; it < maxIterForLowestBound; ++it) {
    printf("(a)it=%d high=%g low=%g\n", it, high, low);
    auto comp = extractComponent(low, indexA);
    bool connects = std::find(comp.begin(), comp.end(), indexB) != comp.end();

    if (connects){
      break;
    }

    high = low;
    low *= 0.1;

    if(high < 1.e3)
      break;    
  }

  if(low < 1.e2){
    density_threshold_ = 0.;
    out.clear();
    return false;    
  }
  
  for (int it = 0; it < bisectIters; ++it) {
    printf("(b)it=%d high=%g low=%g\n", it, high, low);
    double mid = 0.5 * (low + high);
    auto comp = extractComponent(mid, indexA);
    bool connects = std::find(comp.begin(), comp.end(), indexB) != comp.end();
    if (connects) low = mid; else high = mid;
    if (high - low < tol * low) break;
  }
  double thr = low;
  
  auto comp = extractComponent(thr, indexA);
  density_threshold_ = thr;
  
  // ------ (d) コンポーネント抽出 & PCA ------
  Eigen::Matrix3d axes;
  Eigen::Vector3d centroid, evals;
  computePCA3D(comp, axes, centroid, evals);

  glm::vec3 pos(
		static_cast<float>(centroid.x()),
		static_cast<float>(centroid.y()),
		static_cast<float>(centroid.z())
		);

  glm::vec3 rad(
		static_cast<float>(2.0 * std::sqrt(evals(0))),
		static_cast<float>(2.0 * std::sqrt(evals(1))),
		static_cast<float>(2.0 * std::sqrt(evals(2)))
		);

  glm::mat3 R(
	      static_cast<float>(axes(0,0)), static_cast<float>(axes(1,0)), static_cast<float>(axes(2,0)),
	      static_cast<float>(axes(0,1)), static_cast<float>(axes(1,1)), static_cast<float>(axes(2,1)),
	      static_cast<float>(axes(0,2)), static_cast<float>(axes(1,2)), static_cast<float>(axes(2,2))
	      );

  glm::quat q = glm::quat_cast(R);
  out.set(pos, rad, q);
  
  printf("ellipsoid: center=%g %g %g a=%g b=%g c=%g\n", centroid.x(), centroid.y(), centroid.z(), out.radii.x, out.radii.y, out.radii.z);
  
  kdtree_.reset();

  return true;
}

// ────────────────────────────────────────────────────────────────
// 3. 3‑D PCA (covariance ellipsoid)
// ────────────────────────────────────────────────────────────────
void EllipseFitter::computePCA3D(const std::vector<int>& comp,
                                 Eigen::Matrix3d& axes,
                                 Eigen::Vector3d& centroid,
                                 Eigen::Vector3d& evals) const
{
    centroid.setZero();
    for (int idx: comp){
        centroid.x() += (*dataPtr_)[idx].pos[0];
        centroid.y() += (*dataPtr_)[idx].pos[1];
        centroid.z() += (*dataPtr_)[idx].pos[2];
    }
    centroid /= static_cast<double>(comp.size());

    Eigen::Matrix3d C = Eigen::Matrix3d::Zero();
    for (int idx: comp){
        Eigen::Vector3d d;
        d.x() = (*dataPtr_)[idx].pos[0] - centroid.x();
        d.y() = (*dataPtr_)[idx].pos[1] - centroid.y();
        d.z() = (*dataPtr_)[idx].pos[2] - centroid.z();
        C += d * d.transpose();
    }
    C /= static_cast<double>(comp.size());

    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> es(C);

    // 昇順 → 降順へ並べ替え
    Eigen::Matrix3d evecs = es.eigenvectors();
    Eigen::Vector3d evalsAsc = es.eigenvalues();

    Eigen::Matrix3d axesDesc;
    axesDesc.col(0) = evecs.col(2);    // λ2 (最大)
    axesDesc.col(1) = evecs.col(1);    // λ1
    axesDesc.col(2) = evecs.col(0);    // λ0 (最小)

    // 右手系維持（det<0 なら 1 列反転）
    if (axesDesc.determinant() < 0)
        axesDesc.col(2) = -axesDesc.col(2);

    axes  = axesDesc;
    evals << evalsAsc(2), evalsAsc(1), evalsAsc(0);  // 同じ順
}
#endif
