#include "implot.h"
#include "analysis/clump/find_clumps.h"
#include "FileIO/clump_io.h"
#include "interaction/camera.h"

#include "FileIO/snapshot_source.h"

#ifdef _OPENMP
#include <omp.h>
#endif

#include <nanoflann.hpp>

#include <cmath>
#include <limits>

void FindClump::buildMassHistogram(bool useLogScaleX, float& outMin, float& outMax)
{
  massHistogramValues_.clear();
  
  for (size_t i = 0; i < nodeList.size(); ++i) {
    const StructureNode* node = nodeList[i];
    float mass = node->totalMass;
    
    if (useLogScaleX) {
      if (mass > 0.0f)
	mass = std::log10(mass);
      else
	continue;
    }
    
    massHistogramValues_.push_back(mass);
  }
  
  if (massHistogramValues_.empty()) {
    outMin = 0.0f;
    outMax = 1.0f;
    histogramComputed_ = false;
    return;
  }
  
  float massMin = std::numeric_limits<float>::max();
  float massMax = std::numeric_limits<float>::lowest();
  
  for (const auto& mass : massHistogramValues_) {
    massMin = std::min(massMin, mass);
    massMax = std::max(massMax, mass);
  }
  
  if (massMin == massMax)
    massMax = massMin + 1.0f;
  
  outMin = massMin;
  outMax = massMax;
  histogramComputed_ = true;
}

int FindClump::find_parent(std::vector<int>& parent, int i) {
    if (parent[i] != i)
        parent[i] = find_parent(parent, parent[i]);
    return parent[i];
}

void FindClump::union_sets(std::vector<int>& parent, int a, int b) {
    int pa = find_parent(parent, a);
    int pb = find_parent(parent, b);
    if (pa != pb)
        parent[pb] = pa;
}

// Clump detection routine.
void FindClump::findClumps(std::vector<SimulationElement>& originalParticles,
                           float worldToRenderScale,
			   const std::string &var
			   )
{
  std::vector<SimulationElementFiltered> filteredParticles =
    filterParticles(originalParticles, worldToRenderScale, params_.densityThreshold, var);
  printf("number of filtered particles:%zu out of %zu\n"
	 , filteredParticles.size(), originalParticles.size());

  ParticleCloud cloud;
  cloud.pts = filteredParticles; 
  
  size_t numParticles = cloud.pts.size();
  // Initialize union-find: each particle starts as its own representative.
  std::vector<int> parent(numParticles);
  for (size_t i = 0; i < numParticles; i++) {
    parent[i] = i;
  }

  // Build the KD-tree.
  KDTree_t kdTree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10));
  kdTree.buildIndex();

  // radiusSearch expects the squared linking length for L2 distance.
  double searchRadius = params_.linkingLength * params_.linkingLength;
  nanoflann::SearchParameters params(10);

#ifdef _OPENMP
  int numProcs = omp_get_num_procs();
  // Use that count as the thread count.
  omp_set_num_threads(numProcs);
  std::cout << "Using " << numProcs << " threads." << std::endl;
#endif
  
#pragma omp parallel for
  // Search neighbors for each particle and union matching pairs.
  for (int i = 0; i < static_cast<int>(numParticles); i++) {
    double query_pt[3] = { cloud.pts[i].pos[0], cloud.pts[i].pos[1], cloud.pts[i].pos[2] };

    // Get type aliases that depend on KDTree_t.
    typedef typename KDTree_t::IndexType  IndexType;
    typedef typename KDTree_t::DistanceType DistanceType;
    typedef nanoflann::ResultItem<IndexType, DistanceType> MyResultItem;

    // Declare ret_matches with the correct result type.
    std::vector<MyResultItem> ret_matches;

    if(params_.useHsml)
      searchRadius = cloud.pts[i].renderSupportRadius * cloud.pts[i].renderSupportRadius * params_.linkingLength_over_cell_size * params_.linkingLength_over_cell_size;
		
    kdTree.radiusSearch(query_pt, searchRadius, ret_matches, params);
	
    for (const auto& match : ret_matches) {
      size_t neighbor_index = match.first;
      if (neighbor_index == static_cast<size_t>(i))
	continue;
	    
      if(params_.useHsml){
	double LinkingLength_j = cloud.pts[neighbor_index].renderSupportRadius * cloud.pts[neighbor_index].renderSupportRadius * params_.linkingLength_over_cell_size * params_.linkingLength_over_cell_size;
	if(LinkingLength_j > match.second)
	  continue;
      }

#pragma omp critical
      union_sets(parent, i, neighbor_index);
    }
  }
  
  // Map for accumulating statistics per clump.
  // key: representative ID, value: temporary ClumpInfo accumulator.
  std::unordered_map<int, ClumpInfo> clumpMap;
  for (size_t i = 0; i < numParticles; i++) {
    int root = find_parent(parent, i);
    if (clumpMap.find(root) == clumpMap.end()) {
      clumpMap[root] = { root, 0, 0, 0.0, 0.0, {0.0}, 0.0, 0 };
    }
    ClumpInfo& info = clumpMap[root];
    info.count += 1;
    info.totalMass += cloud.pts[i].mass;
    info.pos_cm[0] += cloud.pts[i].pos[0] * cloud.pts[i].mass;
    info.pos_cm[1] += cloud.pts[i].pos[1] * cloud.pts[i].mass;
    info.pos_cm[2] += cloud.pts[i].pos[2] * cloud.pts[i].mass;

    if(cloud.pts[i].type >= 3){
      info.count_star++;
      info.stellarMass += cloud.pts[i].mass;
    }
  }

  // Final center of mass is weighted-position sum divided by total mass.
  for (auto &kv : clumpMap) {
    ClumpInfo &info = kv.second;
    if (info.totalMass > 0) {
      info.pos_cm[0] /= info.totalMass;
      info.pos_cm[1] /= info.totalMass;
      info.pos_cm[2] /= info.totalMass;
    }
  }

  // Check each particle distance to compute clump radius.
  for (size_t i = 0; i < numParticles; i++) {
    int root = find_parent(parent, i);
    ClumpInfo &info = clumpMap[root];
    double dx = cloud.pts[i].pos[0] - info.pos_cm[0];
    double dy = cloud.pts[i].pos[1] - info.pos_cm[1];
    double dz = cloud.pts[i].pos[2] - info.pos_cm[2];
    double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist > info.radius)
      info.radius = dist;
  }

  // Exclude clumps below the minimum particle count.
  std::vector<ClumpInfo> clumpList;
  for (auto &kv : clumpMap) {
    if (kv.second.count >= params_.minParticles)
      clumpList.push_back(kv.second);
  }

  std::sort(clumpList.begin(), clumpList.end(), [](const ClumpInfo &a, const ClumpInfo &b) {
    return a.totalMass > b.totalMass;
  });

  std::unordered_map<int, ClumpInfo> newClumpMap;
  int cumulativeOffset = 0;
  for (size_t i = 0; i < clumpList.size(); i++) {
    // At this point, clumpList[i].clumpID is the old representative group ID.
    int oldGroup = clumpList[i].clumpID;
    clumpList[i].clumpID = static_cast<int>(i); // New clumpID, assigned from 0.
    clumpList[i].startIndex = cumulativeOffset; // Offset is the cumulative particle count.
    cumulativeOffset += clumpList[i].count;
    newClumpMap[oldGroup] = clumpList[i]; // Save the mapping.
  }

  std::vector<std::pair<int, size_t>> groupIndex;
  for (size_t i = 0; i < numParticles; i++) {
    int oldGroup = find_parent(parent, i);
    // Only target clumps.
    if (clumpMap[oldGroup].count < params_.minParticles)
      continue;
    // The new clumpID is newClumpMap[oldGroup].clumpID.
    groupIndex.push_back({ newClumpMap[oldGroup].clumpID, i });
  }

  std::sort(groupIndex.begin(), groupIndex.end(), [](const std::pair<int, size_t>& a, const std::pair<int, size_t>& b) {
    return a.first < b.first;
  });
 
  // Create sortedParticles from the sort result.
  std::vector<SimulationElementFiltered> sortedParticles;
  sortedParticles.resize(groupIndex.size());
  for (size_t i = 0; i < groupIndex.size(); i++)
    sortedParticles[i] = cloud.pts[groupIndex[i].second];  
  
  // Replace ParticleCloud::pts with sortedParticles.
  cloud.pts = sortedParticles;
  printf("clumpListsize=%lu\n", clumpList.size());

  nodeList.resize(clumpList.size());
  for(size_t i=0;i<clumpList.size();i++){
    std::vector<int> indices;
    
    int index_start = clumpList[i].startIndex;
    int count = clumpList[i].count;

    double vpeak = 0.;
    for(int idx = index_start; idx < index_start + count; idx++){
      int original_index = sortedParticles[idx].original_index;
      indices.push_back(original_index);

      if(sortedParticles[idx].type >= 3)
	continue;
      
      double value = sortedParticles[idx].val;
      if(vpeak < value)
	vpeak = value;
    }

    std::vector<StructureNode*> children={};
    StructureNode* node = new StructureNode(indices, params_.densityThreshold, children);

    node->pos_cm[0] = clumpList[i].pos_cm[0];
    node->pos_cm[1] = clumpList[i].pos_cm[1];
    node->pos_cm[2] = clumpList[i].pos_cm[2];
    node->count = clumpList[i].count;
    node->totalMass = clumpList[i].totalMass;
    node->vpeak = vpeak;

    node->count_star = clumpList[i].count_star;
    node->stellarMass = clumpList[i].stellarMass;
    
    node->children = {};
    node->parent = nullptr;
      
    nodeList[i] = node;
  }

  findClumpComputed = true;
  flagFOFComputed = true;
  flagDendrogramComputed = false;
  flagDirty = true;
}

//──────────────────────────────
// Functions corresponding to the pruning module.
//────────────────────────────────────────────────────────────
namespace pruning {
  double density_contrast_ratio(double peak, double saddle) {
    if (!std::isfinite(peak) || peak <= 0.0) {
      return 0.0;
    }
    if (!std::isfinite(saddle) || saddle <= 0.0) {
      return std::numeric_limits<double>::infinity();
    }
    return peak / saddle;
  }

  double density_contrast_ratio(const StructureNode* s) {
    if (!s) {
      return 0.0;
    }
    const double saddle = s->parent ? s->parent->height() : s->vmin;
    return density_contrast_ratio(s->vmax, saddle);
  }

  // all_true: combine multiple is_independent predicates and return true only if all pass.
  std::function<bool(StructureNode*)> all_true(const std::vector<std::function<bool(StructureNode*)>>& funcs) {
    return [=](StructureNode* s) -> bool {
      for (const auto& f : funcs)
        if (!f(s))
          return false;
      return true;
    };
  }

  // min_density_contrast_ratio: peak/saddle density contrast condition.
  std::function<bool(StructureNode*)> min_density_contrast_ratio(double ratio) {
    return [=](StructureNode* s) -> bool {
      return density_contrast_ratio(s) >= ratio;
    };
  }

  // min_peak: minimum peak-value condition.
  std::function<bool(StructureNode*)> min_peak(double peak) {
    return [=](StructureNode* s) -> bool {
      return s->vmax >= peak;
    };
  }

  // min_npix: minimum pixel-count condition.
  std::function<bool(StructureNode*)> min_npix(int npix) {
    return [=](StructureNode* s) -> bool {
      return s->indices.size() >= static_cast<size_t>(npix);
    };
  }

  // _to_prune: find leaves that should be pruned.
  std::vector<StructureNode*> _to_prune(std::vector<StructureNode*>& keep_structures, double min_density_contrast_ratio, int npix) {
    std::vector<StructureNode*> toPrune;

    for (const auto& s : keep_structures) {
      if (!s->isLeaf())
	continue;

      const bool flag =
        density_contrast_ratio(s) >= min_density_contrast_ratio;

      if(flag)
	continue;
	
      if (s->indices.size() > static_cast<size_t>(npix))
	continue;
      
      if (s->parent == nullptr)
	continue;
      
      toPrune.push_back(s);
    }
    
    return toPrune;
  }

  // _make_trunk: build the trunk and remove orphan leaves that fail independence.
  std::vector<StructureNode*> _make_trunk(std::vector<StructureNode*>& keep_structures, double min_density_contrast_ratio, int npix) {
    // Extract structures without parents as the trunk.
    std::vector<StructureNode*> trunk;
    for (auto& s : keep_structures) {
      s->flag_trunk = false;
      if (s->parent == nullptr)
        trunk.push_back(s);
    }
    
    // Remove orphan leaves in the trunk that do not satisfy independence.
    std::vector<StructureNode*> leavesInTrunk;
    for (StructureNode* s : trunk) {
      if (s->isLeaf())
        leavesInTrunk.push_back(s);
    }
    
    for (StructureNode* leaf : leavesInTrunk) {
      const bool flag =
        density_contrast_ratio(leaf) >= min_density_contrast_ratio;

      if(flag || leaf->indices.size() > static_cast<size_t>(npix)){
	leaf->flag_trunk = true;
	continue;
      }
      
      keep_structures.erase(std::remove(keep_structures.begin(), keep_structures.end(), leaf), keep_structures.end());            
      trunk.erase(std::remove(trunk.begin(), trunk.end(), leaf), trunk.end());      
      delete leaf;
    }
    
    // Cache level 0 for each trunk structure for faster access.
    //for (StructureNode* s : trunk)
    //s->_level = 0;

    return trunk;
  }

  void _merge_with_parent(StructureNode *m){
    StructureNode* parent = m->parent;
    if (!parent)
        return;

    parent->merge_node(m);
    parent->children.erase(std::remove(parent->children.begin(), parent->children.end(), m),
			   parent->children.end());

    if (!m->isLeaf()) {
        // Add m's children to the parent's children.
        parent->children.insert(parent->children.end(), m->children.begin(), m->children.end());
        // Redirect each child's parent pointer to the parent.
        for (auto child : m->children) 
            child->parent = parent;        
    }
  }
}


std::vector<int> FindClump::makeDendrogramDensityOrder(
  const std::vector<SimulationElementFiltered>& particles) const
{
  std::vector<int> sortedIndices(particles.size());
  for (size_t i = 0; i < particles.size(); ++i) {
    sortedIndices[i] = static_cast<int>(i);
  }

  std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
    return particles[static_cast<size_t>(a)].density >
           particles[static_cast<size_t>(b)].density;
  });

  return sortedIndices;
}

void FindClump::buildDendrogramHierarchy(const ParticleCloud& cloud,
                                         const std::vector<int>& sortedIndices,
                                         const std::vector<int>& rank)
{
  KDTree_t kdTree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10));
  kdTree.buildIndex();

  nanoflann::SearchParameters params(10);

  std::vector<StructureNode*>& nodes = nodeList;
  std::vector<StructureNode*> leafindex(cloud.pts.size(), nullptr);

  for (int idx : sortedIndices) {
    const SimulationElementFiltered& p = cloud.pts[static_cast<size_t>(idx)];
    double query_pt[3] = { p.pos[0], p.pos[1], p.pos[2] };

    typedef typename KDTree_t::IndexType  IndexType;
    typedef typename KDTree_t::DistanceType DistanceType;
    typedef nanoflann::ResultItem<IndexType, DistanceType> MyResultItem;
    std::vector<MyResultItem> ret_matches;

    double searchRadius =
      p.renderSupportRadius * p.renderSupportRadius *
      params_.linkingLength_over_cell_size *
      params_.linkingLength_over_cell_size;

    kdTree.radiusSearch(query_pt, searchRadius, ret_matches, params);

    std::vector<int> neighborClusters;
    std::vector<int> smallestIndices;
    for (const auto& match : ret_matches) {
      size_t neighbor_index = match.first;
      if (neighbor_index == static_cast<size_t>(idx))
        continue;

      if (rank[neighbor_index] > rank[static_cast<size_t>(idx)])
        continue;

      StructureNode* leaf = leafindex[neighbor_index];
      if (!leaf) continue;
      int smallest_index = leaf->smallest_index;

      if (find(smallestIndices.begin(), smallestIndices.end(), smallest_index) ==
          smallestIndices.end()) {
        neighborClusters.push_back(static_cast<int>(neighbor_index));
        smallestIndices.push_back(smallest_index);
      }
    }

    std::vector<StructureNode*> neighborNodes;
    for (auto i : neighborClusters) {
      StructureNode* leaf = leafindex[static_cast<size_t>(i)];
      if (!leaf) continue;
      neighborNodes.push_back(leaf->ancestor());
    }

    std::sort(neighborNodes.begin(), neighborNodes.end());
    auto last = std::unique(neighborNodes.begin(), neighborNodes.end());
    neighborNodes.erase(last, neighborNodes.end());

    if (neighborNodes.empty()) {
      StructureNode* newLeaf = new StructureNode(std::vector<int>{idx}, p.density);
      nodes.push_back(newLeaf);
      leafindex[static_cast<size_t>(idx)] = newLeaf;
    } else if (neighborNodes.size() == 1) {
      StructureNode* pLeaf = neighborNodes[0];
      leafindex[static_cast<size_t>(idx)] = pLeaf;
      pLeaf->add_particle(idx, p.density);
    } else {
      int count = 0;
      std::vector<StructureNode*> merger;
      for (size_t i = 0; i < neighborNodes.size(); i++) {
        StructureNode* adjacent = neighborNodes[i];
        if (adjacent->isLeaf()) {
          const bool independent =
            pruning::density_contrast_ratio(adjacent) >=
            params_.minDensityContrastRatio;

          if (!independent ||
              adjacent->indices.size() < static_cast<size_t>(params_.minParticles)) {
            merger.push_back(adjacent);
            continue;
          }
        }

        neighborNodes[static_cast<size_t>(count)] = adjacent;
        count++;
      }

      neighborNodes.resize(static_cast<size_t>(count));

      if (neighborNodes.empty()) {
        neighborNodes.push_back(merger.back());
        merger.pop_back();
      }

      StructureNode* pLeaf_rep = nullptr;
      if (neighborNodes.size() == 1) {
        pLeaf_rep = leafindex[static_cast<size_t>(idx)] = neighborNodes[0];
        pLeaf_rep->add_particle(idx, p.density);
      } else {
        StructureNode* mergeNode =
          new StructureNode(std::vector<int>{idx}, p.density, neighborNodes);
        leafindex[static_cast<size_t>(idx)] = pLeaf_rep = mergeNode;
        nodes.push_back(mergeNode);
      }

      while (!merger.empty()) {
        StructureNode* leaf_merged = merger.back();
        merger.pop_back();
        for (int particleID : leaf_merged->indices) {
          leafindex[static_cast<size_t>(particleID)] = pLeaf_rep;
        }

        pLeaf_rep->merge_node(leaf_merged);

        if (leaf_merged->parent) {
          auto& siblings = leaf_merged->parent->children;
          siblings.erase(std::remove(siblings.begin(), siblings.end(), leaf_merged),
                         siblings.end());
        }

        nodes.erase(std::remove(nodes.begin(), nodes.end(), leaf_merged), nodes.end());
        delete leaf_merged;
      }
    }
  }
}

void FindClump::pruneDendrogramHierarchy()
{
  std::vector<StructureNode*>& nodes = nodeList;
  while (true) {
    auto toPrune = pruning::_to_prune(nodes,
                                      params_.minDensityContrastRatio,
                                      params_.minParticles);
    if (toPrune.empty())
      break;
    for (StructureNode* s : toPrune) {
      StructureNode* parent = s->parent;
      if (!parent)
        continue;
      auto& siblings = parent->children;
      std::vector<StructureNode*> merge;
      if (siblings.size() == 2) {
        merge = siblings;
      } else if (siblings.size() > 2) {
        merge.push_back(s);
      }
      for (StructureNode* m : merge) {
        pruning::_merge_with_parent(m);
        nodes.erase(std::remove(nodes.begin(), nodes.end(), m), nodes.end());
        delete m;
      }
    }
  }

  std::vector<StructureNode*> trunk =
    pruning::_make_trunk(nodes,
                         params_.minDensityContrastRatio,
                         params_.minParticles);
  (void)trunk;
}

void FindClump::finalizeDendrogramNodes(
  const std::vector<SimulationElementFiltered>& filteredParticles)
{
  for (StructureNode* node : nodeList) {
    calc_node_statistic(node, filteredParticles);
  }

  sortNodesByMass();

  for (StructureNode* node : nodeList) {
    std::vector<int> indices_new;
    indices_new.reserve(node->indices.size());

    for (int filteredIndex : node->indices) {
      int original_index =
        filteredParticles[static_cast<size_t>(filteredIndex)].original_index;
      indices_new.push_back(original_index);
    }

    node->indices = indices_new;
  }
}

void FindClump::findClumpsDendrogram(std::vector<SimulationElement>& originalParticles,
                                     float worldToRenderScale,
                                     const std::string &var)
{
  std::vector<SimulationElementFiltered> filteredParticles =
    filterDendrogramGasParticles(originalParticles,
                                 worldToRenderScale,
                                 params_.densityThreshold,
                                 var);
  printf("number of dendrogram gas particles:%zu out of %zu\n"
	 , filteredParticles.size(), originalParticles.size());

  if (filteredParticles.empty()) {
    findClumpComputed = true;
    flagFOFComputed = false;
    flagDendrogramComputed = true;
    flagDirty = true;
    return;
  }

  ParticleCloud cloud;
  cloud.pts = filteredParticles;

  const size_t numParticles = cloud.pts.size();
  std::vector<int> sortedIndices = makeDendrogramDensityOrder(cloud.pts);

  std::vector<int> rank(numParticles, -1);
  for (size_t i = 0; i < sortedIndices.size(); ++i)
    rank[sortedIndices[i]] = i;

  buildDendrogramHierarchy(cloud, sortedIndices, rank);
  pruneDendrogramHierarchy();
  finalizeDendrogramNodes(filteredParticles);

  findClumpComputed = true;
  flagFOFComputed = false;
  flagDendrogramComputed = true;
  flagDirty = true;
}

void FindClump::calc_node_statistic(StructureNode *ns, const std::vector<SimulationElementFiltered>& p){
  if(ns->_done_statistics == true)
    return;

  int count = 0, count_star = 0;
  double total_mass = 0., stellar_mass = 0.;
  double pos_cm[3] = {0., 0., 0.};
  double vpeak = ns->vmax;
  
  for(StructureNode *child : ns->children){
    calc_node_statistic(child, p);

    vpeak = std::max(vpeak, child->vpeak);
    
    count += child->count;
    total_mass += child->totalMass;
    pos_cm[0] += child->totalMass * child->pos_cm[0];
    pos_cm[1] += child->totalMass * child->pos_cm[1];
    pos_cm[2] += child->totalMass * child->pos_cm[2];

    count_star += child->count_star;
    stellar_mass += child->stellarMass;
  }
    
  for(int particleID : ns->indices){      
    double mass = p[particleID].mass;
    const float (&pos)[3] = p[particleID].pos;

    count++;
    total_mass += mass;
    pos_cm[0] += mass * pos[0];
    pos_cm[1] += mass * pos[1];
    pos_cm[2] += mass * pos[2];

    if(p[particleID].type >= 3){
      count_star++;
      stellar_mass += mass;
    }      
  }

  if(total_mass > 0.){
    pos_cm[0] /= total_mass;
    pos_cm[1] /= total_mass;
    pos_cm[2] /= total_mass;
  }

  ns->vpeak = vpeak;
  ns->count = count;
  ns->totalMass = total_mass;
  ns->pos_cm[0] = pos_cm[0];
  ns->pos_cm[1] = pos_cm[1];
  ns->pos_cm[2] = pos_cm[2];

  ns->count_star = count_star;
  ns->stellarMass = stellar_mass;
  
  ns->_done_statistics = true;
}


//------------------------------------------------------------
// 1) Sort by mass in descending order.
void FindClump::sortNodesByMass() {
  std::sort(nodeList.begin(), nodeList.end(), [](const StructureNode* a, const StructureNode* b) {
    return a->totalMass > b->totalMass;
  });

  flagDirty = true;
}

//------------------------------------------------------------
// 2) Helper for hierarchy sorting using pre-order traversal.
void FindClump::traverseHierarchy(StructureNode* node, std::vector<StructureNode*>& sortedNodes) {
  if (!node) return;
  sortedNodes.push_back(node);
  // Visit child nodes in order. The order depends on children.
  for (StructureNode* child : node->children) {
    traverseHierarchy(child, sortedNodes);
  }
}

// Sort by hierarchy.
// Start from trunk nodes with parent == nullptr and build pre-order order.
void FindClump::sortNodesByHierarchy() {
  if(!flagDendrogramComputed)
    return;
    
  std::vector<StructureNode*> sortedNodes;
  // Traverse trunk nodes, which are nodes whose parent is nullptr.
  for (StructureNode* node : nodeList) {
    if (node->parent == nullptr) {
      traverseHierarchy(node, sortedNodes);
    }
  }

  nodeList = sortedNodes;
  flagDirty = true;
}


// Example filtering operation.
std::vector<FindClump::SimulationElementFiltered> FindClump::filterParticles(const std::vector<SimulationElement>& particles,
                                                                           float worldToRenderScale,
                                                                           double threshold,
                                                                           const std::string &var) const{
  std::vector<SimulationElementFiltered> filtered;
  for (size_t i = 0; i < particles.size(); ++i) {
      const SimulationElement &p = particles[i];
      if (p.type >= 3 || p.getValue(var) >= threshold) {
	SimulationElementFiltered copy = filter_particle_for_clump_find(p, i, worldToRenderScale, var);
	copy.original_index = static_cast<int>(i);
	filtered.push_back(copy);
      }
  }
  return filtered;
}

std::vector<FindClump::SimulationElementFiltered> FindClump::filterDendrogramGasParticles(
  const std::vector<SimulationElement>& particles,
  float worldToRenderScale,
  double threshold,
  const std::string& var) const
{
  std::vector<SimulationElementFiltered> filtered;
  for (size_t i = 0; i < particles.size(); ++i) {
    const SimulationElement& p = particles[i];
    if (p.type != 0 || p.getValue(var) < threshold) {
      continue;
    }

    SimulationElementFiltered copy =
      filter_particle_for_clump_find(p, i, worldToRenderScale, var);
    copy.original_index = static_cast<int>(i);
    filtered.push_back(copy);
  }
  return filtered;
}

std::vector<SimulationElement> FindClump::getAllChildren(StructureNode* node, std::vector<SimulationElement>& original_p) const{
  std::vector<SimulationElement> pts;
  if (!node)
    return pts;

  // Add indices from the current node.
  for (int idx : node->indices) {
    pts.push_back(original_p[idx]);
  }

  // Recursively process child nodes and merge their indices.
  for (StructureNode* child : node->children) {
    std::vector<SimulationElement> childIndices = getAllChildren(child, original_p);
    pts.insert(pts.end(), childIndices.begin(), childIndices.end());
  }

  return pts;
}




#ifdef CLUMP_DATA_READ
void FindClump::do_FOF_and_output_clump_data(int method,
                                             SimulationBlock& block,
                                             double snapshotTime,
                                             char *filename,
                                             int snapshotIndex){
  char var_name[]="Density";
  std::string var(var_name);
  
  if(method == 0)
    runFOF(block, var);
  else
    runDendrogram(block, var);

  for(auto& node : nodeList)
    node->construct_ID_array(block);

  std::string fname(filename);
  writeFOFtoHDF5(block, snapshotTime, fname, snapshotIndex);

  if(nodeList_prev.size() > 0){
    if(nodeList_prev.size())
      findClumpsInNextSnapshot();
    
    addNextClumpIDtoHDF5(nodeList_prev, fname, snapshotIndex_prev);
  }
    
  nodeList_prev = nodeList;
  snapshotIndex_prev = snapshotIndex;
}


void FindClump::findClumpsInNextSnapshot(void){
  struct ParticleInfo {
    int64_t ID;
    int clumpID;
  };

  std::vector<struct ParticleInfo> particleIDs;

  size_t count = 0;
  for(size_t ic=0;ic< nodeList.size();ic++){
    StructureNode *node = nodeList[ic];

    if(!node->isLeaf())
      continue;

    size_t clumpID = count;    
    for(auto id : node->IDs){
      struct ParticleInfo temp;
      temp.ID = id;
      temp.clumpID = clumpID;

      particleIDs.push_back(temp);      
    }

    count++;
  }
  size_t nClump = count;
  
  std::sort(particleIDs.begin(), particleIDs.end(),
	    [](const auto &a, const auto &b) {
              return a.ID < b.ID;
	    });

  for(auto &node : nodeList_prev){
    if(!node->isLeaf())
      continue;

    std::vector<int> counts(nClump, 0);
    
    size_t i1=0, i2=0;
    while(i1 < node->IDs.size() && i2 < particleIDs.size()){
      if(node->IDs[i1] < particleIDs[i2].ID){
        i1++;
      }
      else if(node->IDs[i1] > particleIDs[i2].ID){
        i2++;
      }
      else {
	int clumpID = particleIDs[i2].clumpID;
	counts[clumpID]++;

        i1++; i2++;
      }
    }

    int max_count = MIN_PARTICLE_OVERLAP;  //at least MIN_PARTICLE_OVERLAP partilces should overlap
    int max_clump_ID = -1;
    for(size_t i=0;i < nClump;i++){
      if(counts[i] > max_count){
	max_count = static_cast<int>(counts[i]);
	max_clump_ID = static_cast<int>(i);
      }
    }

    printf("nextID=%d max_count=%d out of %lu\n", max_clump_ID, max_count, node->IDs.size());
    
    node->clumpID_in_next_snapshot = max_clump_ID;
    node->partfrac_in_next_snapshot = static_cast<float>(max_count)/static_cast<float>(node->IDs.size());
  }
}



void FindClump::writeFOFtoHDF5(const SimulationBlock& block,
			       double snapshotTime,
			       const std::string &filename,
			       int snapshotIndex)
{
  const auto& particles = block.particles;
  size_t count = 0;
  size_t totalParticles = 0;
  
  for (auto& node : nodeList) {
    if(node->isLeaf()){
      totalParticles += node->indices.size();
      count++;
    }
  }

  size_t nClumps = count;

  ClumpInfoIO out;
  out.time = snapshotTime;
  
  out.particle_type.resize(totalParticles);
  out.particle_ids.resize(totalParticles);

  out.clump_id.resize(nClumps);
  out.clump_offset.resize(nClumps);
  out.clump_size.resize(nClumps);
  out.clump_stellar_count.resize(nClumps);
  out.clump_mass.resize(nClumps);
  out.clump_stellar_mass.resize(nClumps);
  out.clump_stellar_mass_maximum.resize(nClumps);
  out.clump_density.resize(nClumps);
  out.clump_max_density.resize(nClumps);
  out.clump_temperature.resize(nClumps);
  out.clump_temperature_density_weighted.resize(nClumps);
  out.clump_position.resize(3*nClumps);
  out.clump_velocity.resize(3*nClumps);

  size_t offset = 0;
  for (size_t i = 0, count=0; i < nodeList.size(); i++) {
    StructureNode* node = nodeList[i];

    if(!node->isLeaf())
      continue;
    
    out.clump_offset[count] = static_cast<int>(offset);

    int stellar_count = 0;
    double mass=0., density=0., temperature=0., position[3]={0.,0.,0.}, velocity[3]={0.,0.,0.};
    double temperature_d = 0., max_density = 0.;
    double gas_mass=0., stellar_mass = 0.;
    double stellar_mass_maximum = 0.;
    for (int idx : node->indices) {
      int64_t pid = block.particleIdSigned(static_cast<size_t>(idx));
      out.particle_ids[offset] = pid;
      out.particle_type[offset] = static_cast<char>(particles[idx].type);
      offset++;

      mass += particles[idx].mass;

      if(particles[idx].type == 0){
	gas_mass += particles[idx].mass;
	density += particles[idx].mass * particles[idx].density;
	temperature += particles[idx].mass * particles[idx].temperature;
	temperature_d += particles[idx].mass * particles[idx].density * particles[idx].temperature;
      }

      if(particles[idx].type >= 3){
	if(particles[idx].mass > stellar_mass_maximum)
	  stellar_mass_maximum = particles[idx].mass;
	
	stellar_mass += particles[idx].mass;
	stellar_count++;
      }
      
      for(int k=0;k<3;k++){
	position[k] += particles[idx].mass * particles[idx].position[k];
	velocity[k] += particles[idx].mass * particles[idx].vel[k];
      }

      if(particles[idx].density > max_density)
	max_density = particles[idx].density;
    }

    if(gas_mass > 0.){      
      density /= gas_mass;
      temperature /= gas_mass;
      temperature_d /= (density * gas_mass);
    }
    
    if(mass){
      for(int k=0;k<3;k++){
	position[k] /= mass;
	velocity[k] /= mass;
      }
    }

    out.clump_id[count] = i;
    out.clump_size[count] = static_cast<int>(node->indices.size());
    out.clump_mass[count] = static_cast<float>(mass);
    out.clump_stellar_mass[count] = static_cast<float>(stellar_mass);
    out.clump_stellar_mass_maximum[count] = static_cast<float>(stellar_mass_maximum);
    out.clump_stellar_count[count] = static_cast<int>(stellar_count);
    out.clump_density[count] = static_cast<float>(density);
    out.clump_max_density[count] = static_cast<float>(density);
    out.clump_temperature[count] = static_cast<float>(temperature);
    out.clump_temperature_density_weighted[count] = static_cast<float>(temperature_d);
    for(int k=0;k<3;k++){    
      out.clump_position[3*i+k] = static_cast<float>(position[k]);
      out.clump_velocity[3*i+k] = static_cast<float>(velocity[k]);
    }

    count++;
  }

  uint32_t mask_out = (L_TIME | L_DENSITY_THRESHOLD | L_CLUMP_ID | L_CLUMP_SIZE | L_CLUMP_OFFSET
		       | L_CLUMP_STELLAR_COUNT | L_CLUMP_STELLAR_MASS | L_CLUMP_STELLAR_MASS_MAXIMUM
		       | L_CLUMP_POSITION | L_CLUMP_VELOCITY | L_CLUMP_DENSITY | L_CLUMP_MAX_DENSITY
		       | L_CLUMP_TEMPERATURE | L_CLUMP_TEMP_DENSITY_WEIGHTED | L_CLUMP_MASS
		       | L_PARTICLE_IDS | L_PARTICLE_TYPE);
  
  ClumpIO::writeSnapshot(filename, snapshotIndex, mask_out, out);     
}

void FindClump::readFOFtoHDF5(const std::string &filename,
			      int snapshotIndex,
			      std::vector<int64_t> &sorted_particle_id,
			      std::vector<int> &clump_id,
			      std::vector<int> &clump_offset,
			      std::vector<int> &clump_size
			      ){
  uint32_t mask = (L_CLUMP_ID | L_CLUMP_SIZE  | L_CLUMP_OFFSET
		   | L_CLUMP_POSITION | L_CLUMP_VELOCITY
		   | L_CLUMP_DENSITY | L_CLUMP_MAX_DENSITY
		   | L_CLUMP_TEMPERATURE | L_CLUMP_TEMP_DENSITY_WEIGHTED
		   | L_CLUMP_MASS | L_PARTICLE_IDS);
  ClumpInfoIO in;    
  bool flag = ClumpIO::readSnapshot(filename, snapshotIndex, mask, in);

  if(flag == false){
    printf("Cannot open the HDF5 file, %s\n", filename.c_str());
    return;
  }
  
  sorted_particle_id = in.particle_ids;
  clump_id = in.clump_id;
  clump_offset = in.clump_offset;
  clump_size = in.clump_size;
}
#endif
