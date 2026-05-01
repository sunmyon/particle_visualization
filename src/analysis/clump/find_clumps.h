#pragma once
#include <algorithm>
#include <cstdint>

#include <nanoflann.hpp>

#include <vector>
#include "data/particle_block.h"
#include "data/particle_coordinates.h"
#include "data/particle_data.h"
#include "analysis/clump/structure_nodes.h"

/**** needed for Im32U ****/
class ProjectionMapGenerator;
struct NormalizationContext;
struct InputFilterConfig;

class FindClump{
public:
  struct ParticleDataFiltered{
    float pos[3];
    float normalized_hsml;
    float density;
    float val;             // Physical quantity in [0,1].
    float mass;            // mass
    uint8_t type;          // Particle type, 0 through 5.
    int64_t ID;
    int original_index;
  };
  
  ParticleDataFiltered filter_particle_for_clump_find(const ParticleData p,
                                                      size_t index,
                                                      float normalizedScale,
                                                      const std::string &var) const{
    ParticleDataFiltered p_f;
    normalizedParticlePosition(p, normalizedScale, p_f.pos);

    p_f.normalized_hsml = normalizedParticleHsml(p, normalizedScale);
    p_f.mass = p.mass;  
    p_f.density = p.density;  
    p_f.val = p.getValue(var);

    p_f.type = p.type;
    p_f.ID = particleBlockForIds_
      ? particleBlockForIds_->particleIdSigned(index)
      : static_cast<int64_t>(index);

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
  std::vector<StructureNode *> nodeList;

#ifdef USE_CONVEX_HULL
  std::vector<bool> showHull_;          // Per-clump convex-hull visibility checkbox state.
#endif
  
  bool findClumpComputed = false;              // Whether FOF results are available.
  bool flagFOFComputed = false;
  bool flagDendrogramComputed = false;
  bool flagDirty = false;

  static constexpr int MIN_PARTICLE_OVERLAP = 10;
  
  struct Params params_;
   
  bool histogramComputed_ = false;
  std::vector<float> massHistogramValues_;
  /************************************/
  
  std::vector<StructureNode *> nodeList_prev; //will be used for tracking clumps
  int snapshotIndex_prev;

  std::vector<StructureNode *> nodeList_next; //will be used for tracking clumps
  const ParticleBlock* particleBlockForIds_ = nullptr;

  void clearNodes(){
    if (findClumpComputed) {
      for (auto node : nodeList)
        delete node;
    }
  }
  
  struct ClumpInfo {
    int clumpID;      // Representative ID in union-find.
    int count;
    int count_star;
    double totalMass; 
    double stellarMass;
    double pos_cm[3]; 
    double radius;
    size_t startIndex;
  };

  struct ParticleCloud {
    std::vector<ParticleDataFiltered> pts;

    inline size_t kdtree_get_point_count() const {
      return pts.size();
    }

    inline double kdtree_get_pt(const size_t idx, const size_t dim) const {
      return pts[idx].pos[dim];
    }

    template <class BBOX>
    bool kdtree_get_bbox(BBOX& /*bb*/) const { return false; }
  };

  // KD-tree type definition for nanoflann.
  typedef nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<double, ParticleCloud>,
    ParticleCloud,
    3  // Dimension count.
    > KDTree_t;
  
  void findClumps(std::vector<ParticleData>& cloud, float normalizedScale, const std::string &var);
  int find_parent(std::vector<int>& parent, int i);
  void union_sets(std::vector<int>& parent, int a, int b);

  void findClumpsDendrogram(std::vector<ParticleData>& cloud, float normalizedScale, const std::string &var);
  void calc_node_statistic(StructureNode *ns, const std::vector<ParticleDataFiltered>& p);
  void traverseHierarchy(StructureNode* node, std::vector<StructureNode*>& sortedNodes);
  std::vector<ParticleDataFiltered> filterParticles(const std::vector<ParticleData>& particles,
                                                       float normalizedScale,
                                                       double threshold,
                                                       const std::string &var) const;
  std::vector<ParticleData> getAllChildren(StructureNode* node, std::vector<ParticleData>& p) const;

  void findClumpsInNextSnapshot(void);
  
  void readFOFtoHDF5(    const std::string &filename,
			 int snapshotIndex,
			 std::vector<int64_t> &sorted_particle_id,
			 std::vector<int> &clump_id,
			 std::vector<int> &clump_offset,
			 std::vector<int> &clump_size);

  // Recursively process indices for this node and its descendants.
  void setFlagsRecursively(StructureNode* node, std::vector<uint8_t>& stressFlags) {
    if (!node)
      return;
    
    // Process indices on the current node.
    for (auto idx : node->indices) {
      if (idx >= 0 && static_cast<size_t>(idx) < stressFlags.size()) {
        stressFlags[static_cast<size_t>(idx)] = 1;
      }
    }
    
    // Recursively apply the same processing to child nodes.
    for (auto child : node->children) {
      setFlagsRecursively(child, stressFlags);
    }
  }
  
public:
  FindClump() = default;

  bool computed() const { return findClumpComputed; }

  bool isDirty() const { return flagDirty; }
  void clearDirtyFlag(){ flagDirty = false; }
  void setDirtyFlag(){ flagDirty = true; }
     
  const std::vector<StructureNode *>& nodes() const { return nodeList; }
  std::vector<StructureNode *>& nodes() { return nodeList; }

  const StructureNode* node(int i) const { return nodeList[i]; }
  StructureNode * node(int i) { return nodeList[i]; }

  Params& params() { return params_; }
  const Params& params() const { return params_; }

  const std::vector<float>& massHistogramValues() const { return massHistogramValues_; }

  void sortNodesByMass();
  void sortNodesByHierarchy();
  
  void runFOF(ParticleBlock& block, const std::string &var){
    particleBlockForIds_ = &block;
    clearNodes();
    findClumps(block.particles, block.normalizedScale, var);
#ifdef USE_CONVEX_HULL
    showHull_.assign(nodeList.size(), false);
#endif
  }

  void runDendrogram(ParticleBlock& block, const std::string &var){
    particleBlockForIds_ = &block;
    clearNodes();
    findClumpsDendrogram(block.particles, block.normalizedScale, var);
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

  void applyHullSelectionToStressFlags(std::vector<uint8_t>& stressFlags,
                                       size_t particleCount) {
    stressFlags.resize(particleCount, 0);
    std::fill(stressFlags.begin(), stressFlags.end(), 0);

    for (size_t i = 0; i < showHull_.size(); ++i) {
      if (!showHull_[i]) continue;
      setFlagsRecursively(nodeList[i], stressFlags);
    }

    setDirtyFlag();
  }
#endif  

  void buildMassHistogram(bool useLogScaleX, float& outMin, float& outMax);
  bool histogramComputed() const { return histogramComputed_; }
  
  void writeFOFtoHDF5(const ParticleBlock& block,
                      double snapshotTime,
                      const std::string &filename,
                      int snapshotIndex);
  
  int get_nclumps() const{
    return nodeList.size();
  }
  
  std::vector<ParticleData> get_particle_indices(int i, std::vector<ParticleData>& originalParticles) const{
    std::vector<ParticleData> pts = getAllChildren(nodeList[i], originalParticles);
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
                                    ParticleBlock& block,
                                    double snapshotTime,
                                    char *filename,
                                    int snpashotIndex);
  
  void initialize_prev_nodes(){
    nodeList_prev = {};
    snapshotIndex_prev = -1;
  }
};
