#pragma once

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
/**** needed for Im32U ****/

class ProjectionMapGenerator;

class StructureNode {
public:
  TrackingVector<int> indices;
  TrackingVector<int> IDs;
  int smallest_index;
  double vmax, vmin, vpeak;
  StructureNode* parent;
  StructureNode* _ancestor;
  TrackingVector<StructureNode*> children;

  int count;
  double totalMass, pos_cm[3];

  int count_star;
  double stellarMass;
  
  bool _done_statistics = false;
  bool flag_trunk = false;

  int clumpID_in_next_snapshot;
  float partfrac_in_next_snapshot;
  
  StructureNode(const TrackingVector<int>& idx, double density = 0.0, TrackingVector<StructureNode*> ch = {}, StructureNode* par = nullptr)
    : indices(idx), vmax(density), vmin(density), parent(par) {
    smallest_index = idx[0];
    _ancestor = par;
    IDs = {};
    
    for(StructureNode *p : ch){
      children.push_back(p);
      p->parent = this;
      p->_ancestor = this;
    }
  }
  
  void construct_ID_array(TrackingVector<ParticleData>& particles){
    IDs={};
    
    for(size_t i=0; i<indices.size();i++){
      int idx = indices[i];
      int ID = particles[idx].ID;
      IDs.push_back(ID);
    }
    
    std::sort(IDs.begin(), IDs.end());
  }  
  
  bool isLeaf() const {
    return children.empty();
  }

  StructureNode* ancestor(){
    if(parent == nullptr)
      return this;

    if(_ancestor == nullptr)
      _ancestor = parent;

    while(_ancestor->parent != nullptr){
      StructureNode *a = _ancestor;
      if(a->_ancestor != nullptr)
	_ancestor = a->_ancestor;
      else
	_ancestor = a->parent;
    }
    
    return _ancestor;
  }
  
  void addChild(StructureNode* child) {
    child->parent = this;
    children.push_back(child);
  }

  void add_particle(int index, double value){
    indices.push_back(index);
    smallest_index = std::min(smallest_index, index);
    vmax = std::max(vmax, value);
    vmin = std::min(vmin, value);
  }
  
  void merge_node(StructureNode *nodemerged){
    indices.insert(indices.end(), nodemerged->indices.begin(), nodemerged->indices.end());
    smallest_index = std::min(smallest_index, nodemerged->smallest_index);
    vmax = std::max(vmax, nodemerged->vmax);
    vmin = std::min(vmin, nodemerged->vmin);
  }

  double height() const {
    if (!children.empty()) {
      double min_val = std::numeric_limits<double>::infinity();
      for (const auto& child : children) 
	min_val = std::min(min_val, child->vmin);      

      return min_val;
    } else 
      return vmax;    
  }
};


class FindClump{
private:
  bool showWindowClumpFinder = false;

  TrackingVector<StructureNode *> nodeList;

#ifdef USE_CONVEX_HULL
  TrackingVector<bool> showHull;          // 各クランプごとに凸包描画をするかどうかのチェック状態
#endif
  
  bool findClumpComputed = false;              // FOFの結果が得られたかどうか
  bool flagFOFComputed = false;
  bool flagDendrogramComputed = false;
  bool flagClearCache = false;
  bool useHsml = true;
  
  float densityThreshold = 10.;
  float minDepth = 1.;    
  int minParticles = 30;

  float linkingLength = 0.01;
  float linkingLength_over_cell_size = 2.;

  TrackingVector<StructureNode *> nodeList_prev; //will be used for tracking clumps
  int snapshotIndex_prev;

  TrackingVector<StructureNode *> nodeList_next; //will be used for tracking clumps
  int snapshotIndex_next;

  TrackingVector<bool> showEvolve;
  bool flagShowClumpEvolution = false;
  bool showWindowClumpList = false;
  int selectedClumpID = -1;

  bool flagShowWindowClumpChainList = false;
  bool flagClumpChainComputed = false;
  static const int MIN_PARTICLE_OVERLAP = 10;
  static const int LENGTH_MINIMUM_CHAIN = 5;
  
  int i_snapshot = 0;
  int selected_chain_index = -1;
  bool flag_button_pushed = false;
  bool flagFileLoaded = false;
  
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

  struct clump_evolution_info{
    int size;
    int offset;
    
    int index;
    int next_index;
    int stellar_count;
    int stellar_id;

    int global_id;

    float stellar_mass;
    float stellar_mass_maximum;
    
    float mass;
    float density;
    float temperature;
    float temperature_d;
    float pos[3];
    
    int snapindex;
    float time;
    
    bool flag_star;

    float getValue(const std::string &var) const{
      if (var == "Density")
	return density;
      else if (var == "Temperature")
	return temperature;
      else if (var == "ClumpMass")
	return mass;
      else if (var == "StellarMass")
	return stellar_mass;
      else {
	std::cerr << "getValue: Unknown variable \"" << var << "\". Returning 0." << std::endl;
	return 0.0f;
      }
    };
  };

  struct clump_properties{
    int first_snapshot;
    int last_snapshot;

    float first_time;
    float last_time;
    
    float density;
    float temperature;
    float temperature_d;

    int SF_snapshot;
    float SF_time;

    int nstar;
    float mstar;
    float mstar_maximum;
    float mass_maximum;

    int stellar_id;
    int global_id;
  };

  TrackingVector<TrackingVector<clump_evolution_info >> clumpLists;
  TrackingVector<TrackingVector<clump_evolution_info *>> clumpChain;
  TrackingVector<clump_properties> clumpChainProps;

  int clumpChainInitFileIndex;
  int clumpChainNsnapshots;
  int clumpChainDFileIndex;
  std::string clumpChainFileName;
  
  CameraContext& camCtx;  

  TrackingVector<StructureNode *> findClumps(TrackingVector<ParticleData>& cloud, int minParticles, const std::string &var);
  int find_parent(TrackingVector<int>& parent, int i);
  void union_sets(TrackingVector<int>& parent, int a, int b);

  TrackingVector<StructureNode *> findClumpsDendrogram(TrackingVector<ParticleData>& cloud, double min_npix, const std::string &var);
  void calc_node_statistic(StructureNode *ns, const TrackingVector<ParticleData>& p);
  void traverseHierarchy(StructureNode* node, TrackingVector<StructureNode*>& sortedNodes);
  void sortNodesByMass(TrackingVector<StructureNode*>& nodes);
  void sortNodesByHierarchy(TrackingVector<StructureNode*>& nodes);
  TrackingVector<ParticleData> filterParticles(const TrackingVector<ParticleData>& particles, double threshold, const std::string &var) const;
  TrackingVector<ParticleData> getAllChildren(StructureNode* node, TrackingVector<ParticleData>& p) const;

  void findClumpsInNextSnapshot(TrackingVector<ParticleData>&particles);
  
  void writeFOFtoHDF5(const TrackingVector<ParticleData>& particles, const HeaderInfo& header, const std::string &filename, int snapshotIndex);
  void readFOFtoHDF5(    const std::string &filename,
			 int snapshotIndex,
			 TrackingVector<int> &sorted_particle_id,
			 TrackingVector<int> &clump_id,
			 TrackingVector<int> &clump_offset,
			 TrackingVector<int> &clump_size);

  void addNextClumpIDtoHDF5(TrackingVector<StructureNode *> nodes, const std::string &filename, int snapshotIndex);
  
    // 再帰的にノードとその子孫の各 indices を処理する関数
  void setFlagsRecursively(StructureNode* node, TrackingVector<ParticleData>& particles) {
    if (!node)
      return;
    
    // 現在のノードの indices を処理
    for (auto idx : node->indices) {
      particles[idx].flag = 1;
    }
    
    // 子ノードに対して同じ処理を再帰的に実行
    for (auto child : node->children) {
      setFlagsRecursively(child, particles);
    }
  }
  
  void DrawVerticalDashedLine(double x_value, const ImU32& col, float thickness = 1.0f, float dash_length = 5.0f, float gap_length = 3.0f);
  float readClumpTime(std::string fname, int snapshotIndex);
  void readClumpEvolution(std::string fname, int snapshotInit, int snapshotEnd, int dsnapshot, int clumpID_init,
			  TrackingVector<float>& times, TrackingVector<ClumpData>& clumps);

  TrackingVector<TrackingVector<clump_evolution_info *>>  make_clump_evolution_chain(int initstep, int nsnapshots, int dstep, std::string fname);
  TrackingVector<clump_properties> calc_chain_properties(TrackingVector<TrackingVector<clump_evolution_info *>>& clumpChain);
  
public:
  FindClump(CameraContext& cam):
    camCtx(cam)
  {}
  
  void ShowFindClumpsUI(TrackingVector<ParticleData>& originalParticles, const HeaderInfo& header);
  
  int get_nclumps() const{
    return nodeList.size();
  }

  TrackingVector<ParticleData> get_particle_indices(int i, TrackingVector<ParticleData>& originalParticles) const{
    TrackingVector<ParticleData> pts = getAllChildren(nodeList[i], originalParticles);
  
    return pts;
  }

#ifdef USE_CONVEX_HULL
  bool flagShowHull(int i) const{
    if(static_cast<size_t>(i) < showHull.size() && showHull[i])
      return true;
    else
      return false;
  }
#endif
  
  bool checkClumpComputation(void) const{
    return (flagFOFComputed || flagDendrogramComputed);
  }

  bool checkClearCache(void) const{
    return flagClearCache;
  }

  void finishClearCache(void){
    flagClearCache = false;
  }

  void showWindow(void){
    showWindowClumpFinder = true;
  }  

  void do_FOF_and_output_clump_data(int method, TrackingVector<ParticleData>&particle, const HeaderInfo& header, char *filename, int snpashotIndex);
  
  void ReadAndShowClumpsUI(ParticleArray *P, int currentFileIndex);

  void showClumpListWindow(){
    showWindowClumpList = true;
  }

  void initialize_prev_nodes(){
    nodeList_prev = {};
    snapshotIndex_prev = -1;
  }

  void showWindowClumpChainList(int initFileIndex, int nsnapshots, int dsnap, std::string fname){
    clumpChainInitFileIndex = initFileIndex;
    clumpChainNsnapshots = nsnapshots;
    clumpChainDFileIndex = dsnap;
    clumpChainFileName = fname;
    
    flagShowWindowClumpChainList = true;
  }
  
  void give_stellar_id_to_clumps(int initstep, int nsnapshots, int dstep, std::string fname);
  void showClumpChainList(ParticleArray *P, ProjectionMapGenerator *proj);
};


