#pragma once
#include <algorithm>
#include <cstdint>

#include <nanoflann.hpp>

#include <vector>
#include "data/simulation_block.h"
#include "data/sample_coordinates.h"
#include "data/simulation_element.h"
#include "analysis/clump/structure_nodes.h"

/**** needed for Im32U ****/
class ProjectionMapGenerator;
struct NormalizationContext;
struct InputFilterConfig;

class FindClump{
public:
  // Detached clump work item. Detection code reads this sample, not the
  // backing SimulationElement storage.
  struct ClumpParticleSample{
    float pos[3];
    float renderSupportRadius;
    float density;
    float val;             // Physical quantity in [0,1].
    float mass;            // mass
    uint8_t type;          // Particle type, 0 through 5.
    int64_t ID;
    int original_index;
  };
  
  bool quantityFromClumpVariable(const std::string& var, QuantityId& out) const
  {
    if (var == "x") {
      out = QuantityId::PosX;
    } else if (var == "y") {
      out = QuantityId::PosY;
    } else if (var == "z") {
      out = QuantityId::PosZ;
    } else if (var == "r") {
      out = QuantityId::Radius;
    } else if (var == "Density") {
      out = QuantityId::Density;
    } else if (var == "Temperature") {
      out = QuantityId::Temperature;
    } else if (var == "Hsml") {
      out = QuantityId::Hsml;
    } else if (var == "Mass") {
      out = QuantityId::Mass;
    } else {
      return false;
    }
    return true;
  }

  float sampleValueForClumpVariable(const SimulationElement& p,
                                    size_t index,
                                    const std::string& var) const
  {
    (void)p;
    QuantityId quantity = QuantityId::Density;
    if (sourceBlock_ && quantityFromClumpVariable(var, quantity)) {
      return sourceBlock_->getQuantityOr(index, quantity);
    }
    return 0.0f;
  }

  ClumpParticleSample makeClumpParticleSample(const SimulationElement& p,
                                              size_t index,
                                              float worldToRenderScale,
                                              const std::string &var) const{
    ClumpParticleSample p_f;
    if (sourceBlock_) {
      sourceBlock_->getVector(index, VectorId::Pos, p_f.pos);
    } else {
      renderPosition(p, worldToRenderScale, p_f.pos);
    }

    p_f.renderSupportRadius = sourceBlock_
      ? sourceBlock_->getQuantityOr(index, QuantityId::Hsml) * worldToRenderScale
      : renderSupportRadius(p, worldToRenderScale);
    p_f.mass = sourceBlock_
      ? sourceBlock_->getQuantityOr(index, QuantityId::Mass)
      : 0.0f;
    p_f.density = sourceBlock_
      ? sourceBlock_->getQuantityOr(index, QuantityId::Density)
      : 0.0f;
    p_f.val = sampleValueForClumpVariable(p, index, var);

    p_f.type = p.type;
    p_f.ID = sourceBlock_
      ? sourceBlock_->particleIdSigned(index)
      : static_cast<int64_t>(index);

    return p_f;
  }

  /******* used for clump find ********/
  struct Params{
    bool useHsml = true;  
    float densityThreshold = 10.;
    float minDensityContrastRatio = 10.;
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
  const SimulationBlock* sourceBlock_ = nullptr;

  void clearNodes(){
    std::vector<StructureNode*> uniqueNodes = nodeList;
    std::sort(uniqueNodes.begin(), uniqueNodes.end());
    uniqueNodes.erase(std::unique(uniqueNodes.begin(), uniqueNodes.end()),
                      uniqueNodes.end());
    for (StructureNode* node : uniqueNodes) {
      delete node;
    }
    nodeList.clear();
#ifdef USE_CONVEX_HULL
    showHull_.clear();
#endif
    findClumpComputed = false;
    flagFOFComputed = false;
    flagDendrogramComputed = false;
    histogramComputed_ = false;
    massHistogramValues_.clear();
    flagDirty = true;
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
    std::vector<ClumpParticleSample> pts;

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
  
  void findClumps(std::vector<SimulationElement>& cloud, float worldToRenderScale, const std::string &var);
  int find_parent(std::vector<int>& parent, int i);
  void union_sets(std::vector<int>& parent, int a, int b);

  void findClumpsDendrogram(std::vector<SimulationElement>& cloud, float worldToRenderScale, const std::string &var);
  std::vector<ClumpParticleSample> filterDendrogramGasParticles(
    const std::vector<SimulationElement>& particles,
    float worldToRenderScale,
    double threshold,
    const std::string& var) const;
  std::vector<int> makeDendrogramDensityOrder(
    const std::vector<ClumpParticleSample>& particles) const;
  void buildDendrogramHierarchy(const ParticleCloud& cloud,
                                const std::vector<int>& sortedIndices,
                                const std::vector<int>& rank);
  void pruneDendrogramHierarchy();
  void finalizeDendrogramNodes(const std::vector<ClumpParticleSample>& filteredParticles);
  void calc_node_statistic(StructureNode *ns, const std::vector<ClumpParticleSample>& p);
  void traverseHierarchy(StructureNode* node, std::vector<StructureNode*>& sortedNodes);
  std::vector<ClumpParticleSample> filterParticles(const std::vector<SimulationElement>& particles,
                                                       float worldToRenderScale,
                                                       double threshold,
                                                       const std::string &var) const;
  std::vector<SimulationElement> getAllChildren(StructureNode* node, std::vector<SimulationElement>& p) const;

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
  
  void runFOF(SimulationBlock& block, const std::string &var){
    sourceBlock_ = &block;
    clearNodes();
    findClumps(block.particles, block.worldToRenderScale, var);
#ifdef USE_CONVEX_HULL
    showHull_.assign(nodeList.size(), false);
#endif
  }

  void runDendrogram(SimulationBlock& block, const std::string &var){
    sourceBlock_ = &block;
    clearNodes();
    findClumpsDendrogram(block.particles, block.worldToRenderScale, var);
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
  
  void writeFOFtoHDF5(const SimulationBlock& block,
                      double snapshotTime,
                      const std::string &filename,
                      int snapshotIndex);
  
  int get_nclumps() const{
    return nodeList.size();
  }
  
  std::vector<SimulationElement> get_particle_indices(int i, std::vector<SimulationElement>& originalParticles) const{
    std::vector<SimulationElement> pts = getAllChildren(nodeList[i], originalParticles);
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
                                    SimulationBlock& block,
                                    double snapshotTime,
                                    char *filename,
                                    int snpashotIndex);
  
  void initialize_prev_nodes(){
    nodeList_prev = {};
    snapshotIndex_prev = -1;
  }
};
