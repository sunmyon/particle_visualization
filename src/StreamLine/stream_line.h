// StreamlineComputer.h
#pragma once
#include <vector>
#include <Eigen/Core>
#include <Eigen/Dense>
#include <nanoflann.hpp>

struct Vec3 {
  float x,y,z;
  Vec3 operator-(const Vec3&o) const { return {x-o.x, y-o.y, z-o.z}; }
  Vec3 operator+(const Vec3&o) const { return {x+o.x, y+o.y, z+o.z}; }
  Vec3 operator*(float s) const { return Vec3{x*s, y*s, z*s}; }
};

static float dot(const Vec3&a,const Vec3&b){
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

static float len(const Vec3&a){
  return sqrt(a.x*a.x + a.y*a.y + a.z*a.z);
}


struct StreamlineMeshData {
    std::vector<float>   vertices;  // x,y,z, x,y,z, …
    std::vector<size_t>  firsts;    // 各ラインの開始オフセット
    std::vector<size_t>  counts;    // 各ラインの頂点数
};

class StreamlineComputer {
 public:
  void setRegionFromParticleData(TrackingVector<ParticleData>& particles);
  void setRegionByHand(float *center, float *len);
  void setSeeds(TrackingVector<ParticleData>& particles, int lines_per_particle);
  
  void build(TrackingVector<ParticleData>& particles, double theta);
  const StreamlineMeshData& meshData() const { return m_mesh; }

  void disableRegion(void){
    flagSetRegionByHand = false;
  }

  void setStreamRegionFromParticleData(TrackingVector<ParticleData>& particles);
  void setStreamRegionByHand(float *center, float *len);
  void disableStreamRegion(void){
    flagSetStreamRegionByHand = false;
  }
  
 private:
  struct particle_stream{
    float pos[3];
    float vect[3];
  };

  struct StreamParticleCloud {
    TrackingVector<particle_stream> particles;

    // kd-tree インターフェース
    inline size_t kdtree_get_point_count() const { return particles.size(); }
    
    // 指定インデックスの次元 dim の値を返す
    inline float kdtree_get_pt(const size_t idx, const size_t dim) const {
      return particles[idx].pos[dim];
    }
    
    // バウンディングボックスは省略（falseを返す）
    template <class BBOX>
    bool kdtree_get_bbox(BBOX & /*bb*/) const { return false; }
  } cloud;

  // kd-treeの型定義（3次元用）
  typedef nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<float, StreamParticleCloud>,
    StreamParticleCloud,
    3 /* dim */
    > KDTreeType;

  std::unique_ptr<KDTreeType>  m_kdTree;
  
  // CPU上のパイプライン
  void estimate_gradB(TrackingVector<ParticleData> &particle);

  void evalFieldAt(const float x[3], float outB[3], float &hsml);
  std::vector<Vec3> integrateBiStreamline(const Vec3& seed, float h, int maxstep);
  std::vector<Vec3> integrateStreamline(const Vec3& seed, float h, int maxstep);
  std::vector<Vec3> sampleByCurvature(const std::vector<Vec3>& fullLine, float theta);
  
  void flattenLines_();
  Vec3 RK45stepAdaptive(const Vec3 &x, float &h, float tol);
  Vec3 EulerstepAdaptive(const Vec3 &x, float &h, float tol);
  
  bool is_inside_(Vec3 &pos);
  
  static const int max_stream_lines = 10;
  static const int N_neighbours = 20;
  static const int MaxStep = 10000;

  using IndexType    = KDTreeType::IndexType;
  using DistanceType = KDTreeType::DistanceType;
  
  IndexType    indices_list[N_neighbours];
  DistanceType distances_list2[N_neighbours];
  
  double radius_list = -1;
  double dist_nearest = 0.;
  double x_ref[3] = {0.,0.,0.};
    
  // メンバ変数
  std::vector<ParticleData>        m_particles;
  std::vector<Vec3>                m_seeds;
  std::vector<float>               m_hsmls;
  std::vector<std::vector<Vec3>>    m_lines;  // サンプリング後の各ストリームライン

  TrackingVector<std::array<Eigen::Vector3f,3>> gradB;
  TrackingVector<float> r_neighbours;

  float xmin_seed[3], xmax_seed[3]; 
  float xmin_[3], xmax_[3]; 
  StreamlineMeshData  m_mesh;

  bool flagSetRegionByHand=false;
  bool flagSetStreamRegionByHand=false;
};
