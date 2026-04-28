#pragma once
#include "core/tracking_vector.h"
#include "data/particle_data.h"

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
