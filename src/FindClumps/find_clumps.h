#pragma once
#include <nanoflann.hpp>

#include "core/tracking_vector.h"
#include "data/particle_data.h"
#include "FindClumps/structure_nodes.h"

/**** needed for Im32U ****/
class ProjectionMapGenerator;
struct NormalizationContext;
struct InputFilterConfig;

class FindClump{
public:
  struct ParticleDataFiltered{
    float pos[3];
    float Hsml;
    float density;
    float val;             // 物理量（0～1）
    float mass;            // mass
    uint8_t type;          // 粒子タイプ (0～5)
    int ID;
    int original_index;
  };
  
  ParticleDataFiltered filter_particle_for_clump_find(const ParticleData p, const std::string &var) const{
    ParticleDataFiltered p_f;
    p_f.pos[0] = p.pos[0];
    p_f.pos[1] = p.pos[1];
    p_f.pos[2] = p.pos[2];

    p_f.Hsml = p.Hsml;
    p_f.mass = p.mass;  
    p_f.density = p.density;  
    p_f.val = p.getValue(var);

    p_f.type = p.type;
    p_f.ID = p.ID;

    return p_f;
  }

  /******* used for clump find ********/
  struct Params{
    bool useHsml = true;  
    float densityThreshold = 10.;
    float minDepth = 1.;    
    int minParticles = 30;

    float linkingLength = 0.01;
    float linkingLength_over_cell_size = 2.;
  };
  
private:
  TrackingVector<StructureNode *> nodeList;

#ifdef USE_CONVEX_HULL
  TrackingVector<bool> showHull_;          // 各クランプごとに凸包描画をするかどうかのチェック状態
#endif
  
  bool findClumpComputed = false;              // FOFの結果が得られたかどうか
  bool flagFOFComputed = false;
  bool flagDendrogramComputed = false;
  bool flagDirty = false;

  static constexpr int MIN_PARTICLE_OVERLAP = 10;
  
  struct Params params_;
   
  bool histogramComputed_ = false;
  TrackingVector<float> massHistogramValues_;
  /************************************/
  
  TrackingVector<StructureNode *> nodeList_prev; //will be used for tracking clumps
  int snapshotIndex_prev;

  TrackingVector<StructureNode *> nodeList_next; //will be used for tracking clumps

  void clearNodes(){
    if (findClumpComputed) {
      for (auto node : nodeList)
        delete node;
    }
  }
  
  struct ClumpInfo {
    int clumpID;      // union–find での代表番号
    int count;
    int count_star;
    double totalMass; 
    double stellarMass;
    double pos_cm[3]; 
    double radius;
    size_t startIndex;
  };

  struct ParticleCloud {
    TrackingVector<ParticleDataFiltered> pts;

    inline size_t kdtree_get_point_count() const {
      return pts.size();
    }

    inline double kdtree_get_pt(const size_t idx, const size_t dim) const {
      return pts[idx].pos[dim];
    }

    template <class BBOX>
    bool kdtree_get_bbox(BBOX& /*bb*/) const { return false; }
  };

  // nanoflann 用 KD-Treeの型定義
  typedef nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<double, ParticleCloud>,
    ParticleCloud,
    3  // 次元数
    > KDTree_t;
  
  void findClumps(TrackingVector<ParticleData>& cloud, const std::string &var);
  int find_parent(TrackingVector<int>& parent, int i);
  void union_sets(TrackingVector<int>& parent, int a, int b);

  void findClumpsDendrogram(TrackingVector<ParticleData>& cloud, const std::string &var);
  void calc_node_statistic(StructureNode *ns, const TrackingVector<ParticleDataFiltered>& p);
  void traverseHierarchy(StructureNode* node, TrackingVector<StructureNode*>& sortedNodes);
  TrackingVector<ParticleDataFiltered> filterParticles(const TrackingVector<ParticleData>& particles, double threshold, const std::string &var) const;
  TrackingVector<ParticleData> getAllChildren(StructureNode* node, TrackingVector<ParticleData>& p) const;

  void findClumpsInNextSnapshot(void);
  
  void readFOFtoHDF5(    const std::string &filename,
			 int snapshotIndex,
			 TrackingVector<int> &sorted_particle_id,
			 TrackingVector<int> &clump_id,
			 TrackingVector<int> &clump_offset,
			 TrackingVector<int> &clump_size);

  // 再帰的にノードとその子孫の各 indices を処理する関数
  void setFlagsRecursively(StructureNode* node, TrackingVector<ParticleData>& particles) {
    if (!node)
      return;
    
    // 現在のノードの indices を処理
    for (auto idx : node->indices) {
      particles[idx].flag_stress = 1;
    }
    
    // 子ノードに対して同じ処理を再帰的に実行
    for (auto child : node->children) {
      setFlagsRecursively(child, particles);
    }
  }
  
public:
  FindClump() = default;

  bool computed() const { return findClumpComputed; }

  bool isDirty() const { return flagDirty; }
  void clearDirtyFlag(){ flagDirty = false; }
  void setDirtyFlag(){ flagDirty = true; }
     
  const TrackingVector<StructureNode *>& nodes() const { return nodeList; }
  TrackingVector<StructureNode *>& nodes() { return nodeList; }

  const StructureNode* node(int i) const { return nodeList[i]; }
  StructureNode * node(int i) { return nodeList[i]; }

  Params& params() { return params_; }
  const Params& params() const { return params_; }

  const TrackingVector<float>& massHistogramValues() const { return massHistogramValues_; }    

  void sortNodesByMass();
  void sortNodesByHierarchy();
  
  void runFOF(TrackingVector<ParticleData>& cloud, const std::string &var){
    clearNodes();
    findClumps(cloud, var);
#ifdef USE_CONVEX_HULL
    showHull_.assign(nodeList.size(), false);
#endif
  }

  void runDendrogram(TrackingVector<ParticleData>& cloud, const std::string &var){
    clearNodes();
    findClumpsDendrogram(cloud, var);
#ifdef USE_CONVEX_HULL
    showHull_.assign(nodeList.size(), false);
#endif
  }

#ifdef USE_CONVEX_HULL
  bool showHull(int i) const { return showHull_[i]; }

  void setShowHull(int i, bool value) {
    showHull_[i] = value;
  }

  void resetHullFlags() {
    showHull_.assign(nodeList.size(), false);
  }

  void applyHullSelectionToParticles(TrackingVector<ParticleData>& particles) {
    for (auto& p : particles) {
      p.flag_stress = 0;
    }

    for (size_t i = 0; i < showHull_.size(); ++i) {
      if (!showHull_[i]) continue;
      setFlagsRecursively(nodeList[i], particles);
    }

    setDirtyFlag();
  }
#endif  

  void buildMassHistogram(bool useLogScaleX, float& outMin, float& outMax);
  bool histogramComputed() const { return histogramComputed_; }
  
  void writeFOFtoHDF5(const TrackingVector<ParticleData>& particles,
                      double snapshotTime,
                      const std::string &filename,
                      int snapshotIndex);
  
  int get_nclumps() const{
    return nodeList.size();
  }
  
  TrackingVector<ParticleData> get_particle_indices(int i, TrackingVector<ParticleData>& originalParticles) const{
    TrackingVector<ParticleData> pts = getAllChildren(nodeList[i], originalParticles);
    return pts;
  }

#ifdef USE_CONVEX_HULL
  bool flagShowHull(int i) const{
    if(static_cast<size_t>(i) < showHull_.size() && showHull_[i])
      return true;
    else
      return false;
  }
#endif
  
  bool checkClumpComputation(void) const{
    return (flagFOFComputed || flagDendrogramComputed);
  }

  void do_FOF_and_output_clump_data(int method,
                                    TrackingVector<ParticleData>&particle,
                                    double snapshotTime,
                                    char *filename,
                                    int snpashotIndex);
  
  void initialize_prev_nodes(){
    nodeList_prev = {};
    snapshotIndex_prev = -1;
  }
};
