#ifdef GEOMETRICAL_ANALYSIS
// EllipseFitter.cpp
#include "main.h"
#include "ellipse_fitter.h"

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


void EllipseFitter::computeEllipse(const TrackingVector<ParticleData>& data, int ID_A, int ID_B) 
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
    return;
  }

  printf("found!\n");
  
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
    ellipsoid_.a = 0.;
    ellipsoid_.b = 0.;
    ellipsoid_.c = 0.;

    return;    
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
    ellipsoid_.a = 0.;
    ellipsoid_.b = 0.;
    ellipsoid_.c = 0.;

    return;    
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
  
  // 固有値は昇順 λ0<=λ1<=λ2 なので反転して a>=b>=c
  ellipsoid_.a = 2.0*std::sqrt(evals(0)); // k=2.0 ⇒ 95% 近傍 (目安)
  ellipsoid_.b = 2.0*std::sqrt(evals(1));
  ellipsoid_.c = 2.0*std::sqrt(evals(2));
  ellipsoid_.center = centroid;
  ellipsoid_.axes   = axes;
  
  printf("ellipsoid: center=%g %g %g a=%g b=%g c=%g\n", centroid.x(), centroid.y(), centroid.z(), ellipsoid_.a, ellipsoid_.b, ellipsoid_.c);
  
  kdtree_.reset();
}

glm::mat4 EllipseFitter::getModelMatrix() const
{
    const auto& ell = ellipsoid_;

    // 平行移動
    glm::mat4 T = glm::translate(glm::mat4(1.f), glm::vec3(float(ell.center.x()),
                                                          float(ell.center.y()),
                                                          float(ell.center.z())));
    // 回転 (principal axes)
    glm::mat4 R(
        glm::vec4(float(ell.axes(0,0)), float(ell.axes(1,0)), float(ell.axes(2,0)), 0.f),
        glm::vec4(float(ell.axes(0,1)), float(ell.axes(1,1)), float(ell.axes(2,1)), 0.f),
        glm::vec4(float(ell.axes(0,2)), float(ell.axes(1,2)), float(ell.axes(2,2)), 0.f),
        glm::vec4(0.f, 0.f, 0.f, 1.f)
    );

    // スケール – 3 軸 a,b,c
    glm::mat4 S = glm::scale(glm::mat4(1.f), glm::vec3(float(ell.a),
                                                       float(ell.b),
                                                       float(ell.c)));
    return T * R * S;
}

void EllipseFitter::getEllipsoids(double *a, double *b, double *c, double *n){
  *a = ellipsoid_.a;
  *b = ellipsoid_.b;
  *c = ellipsoid_.c;
  *n = density_threshold_;
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
