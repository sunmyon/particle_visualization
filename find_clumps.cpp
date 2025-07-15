#include "main.h"
#include "implot.h"
#include "find_clumps.h"
#include "find_clumps_IO.h"
#include "camera.h"
#include "file_io.h"

#ifdef _OPENMP
#include <omp.h>
#endif

#include <nanoflann.hpp>

namespace{
  struct ParticleCloud {
    TrackingVector<ParticleData> pts;

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
}


int FindClump::find_parent(TrackingVector<int>& parent, int i) {
    if (parent[i] != i)
        parent[i] = find_parent(parent, parent[i]);
    return parent[i];
}

void FindClump::union_sets(TrackingVector<int>& parent, int a, int b) {
    int pa = find_parent(parent, a);
    int pb = find_parent(parent, b);
    if (pa != pb)
        parent[pb] = pa;
}

// クランプ検出ルーチン
TrackingVector<StructureNode *> FindClump::findClumps(TrackingVector<ParticleData>& originalParticles,
						      int minParticles,
						      const std::string &var
						      )
{
  TrackingVector<ParticleData> filteredParticles = filterParticles(originalParticles, densityThreshold, var);
  printf("number of filtered particles:%zu out of %zu\n"
	 , filteredParticles.size(), originalParticles.size());

  ParticleCloud cloud;
  cloud.pts = filteredParticles; 
  
  size_t numParticles = cloud.pts.size();
  // union–find の初期化：各粒子は初めは自分自身の代表
  TrackingVector<int> parent(numParticles);
  for (size_t i = 0; i < numParticles; i++) {
    parent[i] = i;
  }

  // KD-Tree の構築
  KDTree_t kdTree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10));
  kdTree.buildIndex();

  // radiusSearch には、リンク長の二乗値を指定（L2 距離の場合）
  double searchRadius = linkingLength * linkingLength;
  nanoflann::SearchParameters params(10);

#ifdef _OPENMP
  int numProcs = omp_get_num_procs();
  // その数をスレッド数として設定                                                                                                                                                                                                                                                                      
  omp_set_num_threads(numProcs);
  std::cout << "Using " << numProcs << " threads." << std::endl;
#endif
  
#pragma omp parallel for
  // 各粒子について近傍探索し、対象同士を union する
  for (size_t i = 0; i < numParticles; i++) {
    double query_pt[3] = { cloud.pts[i].pos[0], cloud.pts[i].pos[1], cloud.pts[i].pos[2] };

    // KDTree_t に依存する型エイリアスの取得
    typedef typename KDTree_t::IndexType  IndexType;
    typedef typename KDTree_t::DistanceType DistanceType;
    typedef nanoflann::ResultItem<IndexType, DistanceType> MyResultItem;

    // ret_matches を正しい型で宣言する
    std::vector<MyResultItem> ret_matches;

    if(useHsml)
      searchRadius = cloud.pts[i].Hsml * cloud.pts[i].Hsml * linkingLength_over_cell_size * linkingLength_over_cell_size;
		
    kdTree.radiusSearch(query_pt, searchRadius, ret_matches, params);
	
    for (const auto& match : ret_matches) {
      size_t neighbor_index = match.first;
      if (neighbor_index == i)
	continue;
	    
      if(useHsml){
	double LinkingLength_j = cloud.pts[neighbor_index].Hsml * cloud.pts[neighbor_index].Hsml * linkingLength_over_cell_size * linkingLength_over_cell_size;
	if(LinkingLength_j > match.second)
	  continue;
      }

#pragma omp critical
      union_sets(parent, i, neighbor_index);
    }
  }
  
  // クランプ毎の統計量を集計するためのマップ
  // key: 代表番号, value: ClumpInfo を一時的に集計
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

  // 最終的な重心は各クランプの (重心和 / 総質量) とする
  for (auto &kv : clumpMap) {
    ClumpInfo &info = kv.second;
    if (info.totalMass > 0) {
      info.pos_cm[0] /= info.totalMass;
      info.pos_cm[1] /= info.totalMass;
      info.pos_cm[2] /= info.totalMass;
    }
  }

  // クランプサイズ（radius）を計算するため、各粒子の距離をチェック
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

  // 最小粒子数未満のクランプを除外してリストにする
  TrackingVector<ClumpInfo> clumpList;
  for (auto &kv : clumpMap) {
    if (kv.second.count >= minParticles)
      clumpList.push_back(kv.second);
  }

  std::sort(clumpList.begin(), clumpList.end(), [](const ClumpInfo &a, const ClumpInfo &b) {
    return a.totalMass > b.totalMass;
  });

  std::unordered_map<int, ClumpInfo> newClumpMap;
  int cumulativeOffset = 0;
  for (size_t i = 0; i < clumpList.size(); i++) {
    // ここで、clumpList[i].clumpID は元の代表番号（old group id）
    int oldGroup = clumpList[i].clumpID;
    clumpList[i].clumpID = static_cast<int>(i); // 新 clumpID：0から順に
    clumpList[i].startIndex = cumulativeOffset; // offset = これまでの粒子数の合計
    cumulativeOffset += clumpList[i].count;
    newClumpMap[oldGroup] = clumpList[i]; // マッピングを保存
  }

  TrackingVector<std::pair<int, size_t>> groupIndex;
  for (size_t i = 0; i < numParticles; i++) {
    int oldGroup = find_parent(parent, i);
    // 対象となるクランプのみ
    if (clumpMap[oldGroup].count < minParticles)
      continue;
    // 新 clumpID は newClumpMap[oldGroup].clumpID
    groupIndex.push_back({ newClumpMap[oldGroup].clumpID, i });
  }

  std::sort(groupIndex.begin(), groupIndex.end(), [](const std::pair<int, size_t>& a, const std::pair<int, size_t>& b) {
    return a.first < b.first;
  });
 
  // ⑤ ソート結果に基づいて、sortedParticles を作成
  TrackingVector<ParticleData> sortedParticles;
  sortedParticles.resize(groupIndex.size());
  for (size_t i = 0; i < groupIndex.size(); i++)
    sortedParticles[i] = cloud.pts[groupIndex[i].second];  
  
  // (4) ParticleCloud::pts を sortedParticles に置き換える
  cloud.pts = sortedParticles;

  printf("clumpListsize=%lu\n", clumpList.size());
  
  TrackingVector<StructureNode *> nodeList;
  nodeList.resize(clumpList.size());
  for(size_t i=0;i<clumpList.size();i++){
    TrackingVector<int> indices;
    
    int index_start = clumpList[i].startIndex;
    int count = clumpList[i].count;

    double vpeak = 0.;
    for(int idx = index_start; idx < index_start + count; idx++){
      int original_index = sortedParticles[idx].flag;
      indices.push_back(original_index);

      if(sortedParticles[idx].type >= 3)
	continue;
      
      double value = sortedParticles[idx].getValue(var);
      if(vpeak < value)
	vpeak = value;
    }

    TrackingVector<StructureNode*> children={};
    StructureNode* node = new StructureNode(indices, densityThreshold, children);

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
  
  return nodeList;
}

//──────────────────────────────
// pruning モジュール相当の関数群
//────────────────────────────────────────────────────────────
namespace pruning {
  // all_true: 複数の is_independent 関数をまとめ、すべて true なら true を返す
  std::function<bool(StructureNode*)> all_true(const TrackingVector<std::function<bool(StructureNode*)>>& funcs) {
    return [=](StructureNode* s) -> bool {
      for (const auto& f : funcs)
        if (!f(s))
          return false;
      return true;
    };
  }

  // min_delta: delta 条件を返す関数
  std::function<bool(StructureNode*)> min_delta(double delta) {
    return [=](StructureNode* s) -> bool {
      if (s->parent)
        return ((s->height() - s->parent->height()) >= delta);
      return ((s->vmax - s->vmin) >= delta);
    };
  }

  // min_peak: 最低ピーク値条件
  std::function<bool(StructureNode*)> min_peak(double peak) {
    return [=](StructureNode* s) -> bool {
      return s->vmax >= peak;
    };
  }

  // min_npix: 最低ピクセル数条件
  std::function<bool(StructureNode*)> min_npix(int npix) {
    return [=](StructureNode* s) -> bool {
      return s->indices.size() >= static_cast<size_t>(npix);
    };
  }

  // _to_prune: プルーニング対象の leaf を探す
  TrackingVector<StructureNode*> _to_prune(TrackingVector<StructureNode*>& keep_structures, double min_delta, int npix) {
    TrackingVector<StructureNode*> toPrune;

    for (const auto& s : keep_structures) {
      if (!s->isLeaf())
	continue;

      bool flag;
      if (s->parent)
        flag = ((s->height() - s->parent->height()) >= min_delta);
      else
	flag = ((s->vmax - s->vmin) >= min_delta);

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

  // _make_trunk: trunk を作成し、独立性を満たさない孤立リーフを削除する
  TrackingVector<StructureNode*> _make_trunk(TrackingVector<StructureNode*>& keep_structures, double min_delta, int npix) {
    // 親を持たない構造を trunk として抽出
    TrackingVector<StructureNode*> trunk;
    for (auto& s : keep_structures) {
      s->flag_trunk = false;
      if (s->parent == nullptr)
        trunk.push_back(s);
    }
    
    // trunk 内のリーフのうち、独立性を満たさない orphan を削除
    TrackingVector<StructureNode*> leavesInTrunk;
    for (StructureNode* s : trunk) {
      if (s->isLeaf())
        leavesInTrunk.push_back(s);
    }
    
    for (StructureNode* leaf : leavesInTrunk) {
      bool flag;
      if (leaf->parent)
        flag = ((leaf->height() - leaf->parent->height()) >= min_delta);
      else
	flag = ((leaf->vmax - leaf->vmin) >= min_delta);

      if(flag || leaf->indices.size() > static_cast<size_t>(npix)){
	leaf->flag_trunk = true;
	continue;
      }
      
      keep_structures.erase(std::remove(keep_structures.begin(), keep_structures.end(), leaf), keep_structures.end());            
      trunk.erase(std::remove(trunk.begin(), trunk.end(), leaf), trunk.end());      
    }
    
    // trunk 内の各構造のレベルを 0 にキャッシュ（高速化のため）
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
        // m の children を親の children に追加
        parent->children.insert(parent->children.end(), m->children.begin(), m->children.end());
        // 各子の親ポインタを親に変更
        for (auto child : m->children) 
            child->parent = parent;        
    }
  }
}


TrackingVector<StructureNode *> FindClump::findClumpsDendrogram(TrackingVector<ParticleData>& originalParticles,
								double min_npix, const std::string &var)
{
  TrackingVector<ParticleData> filteredParticles = filterParticles(originalParticles, densityThreshold, var);
  printf("number of filtered particles:%zu out of %zu\n"
	 , filteredParticles.size(), originalParticles.size());

  float maxd = 0.;
  for(auto &p : filteredParticles){
    if(p.type != 0)
      continue;
    
    if(maxd < p.density)
      maxd = p.density;
  }

  for(auto &p : filteredParticles){
    if(p.type != 0)
      p.density = maxd;
  }
      
  ParticleCloud cloud;
  cloud.pts = filteredParticles; 
  
  size_t numParticles = cloud.pts.size();

  // 粒子を密度の降順（高密度から）にソートする
  TrackingVector<int> sortedIndices(numParticles);
  for (size_t i = 0; i < numParticles; ++i)
    sortedIndices[i] = i;

  sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
    return cloud.pts[a].density > cloud.pts[b].density;
  });

  TrackingVector<int> rank(numParticles, -1);
  for (size_t i = 0; i < sortedIndices.size(); ++i) 
    rank[sortedIndices[i]] = i;  
  
  // KD-Tree の構築
  KDTree_t kdTree(3, cloud, nanoflann::KDTreeSingleIndexAdaptorParams(10));
  kdTree.buildIndex();

  nanoflann::SearchParameters params(10);

  TrackingVector<StructureNode*> nodes;
  TrackingVector<StructureNode*> leafindex(numParticles);

  //int currentClusterID = 0;  
  
  for (int idx : sortedIndices) {
    const ParticleData& p = cloud.pts[idx];
    double query_pt[3] = { p.pos[0], p.pos[1], p.pos[2] };

    // KDTree_t に依存する型エイリアスの取得
    typedef typename KDTree_t::IndexType  IndexType;
    typedef typename KDTree_t::DistanceType DistanceType;
    typedef nanoflann::ResultItem<IndexType, DistanceType> MyResultItem;

    // ret_matches を正しい型で宣言する
    std::vector<MyResultItem> ret_matches;
    
    double searchRadius = p.Hsml * p.Hsml * linkingLength_over_cell_size * linkingLength_over_cell_size;
    
    kdTree.radiusSearch(query_pt, searchRadius, ret_matches, params);

    TrackingVector<int> neighborClusters;
    TrackingVector<int> smallestIndices;
    for (const auto& match : ret_matches) {
      size_t neighbor_index = match.first;
      if (neighbor_index == static_cast<size_t>(idx))
	continue;

      if(rank[neighbor_index] > rank[idx])
	continue;
      
      int rep = neighbor_index;
      int smallest_index = leafindex[rep]->smallest_index;

      if (find(smallestIndices.begin(), smallestIndices.end(), smallest_index) == smallestIndices.end()){
	neighborClusters.push_back(rep);
	smallestIndices.push_back(smallest_index);
      }
    }

    TrackingVector<StructureNode *> neighborNodes;
    for (auto i : neighborClusters){
      StructureNode *p = leafindex[i];
      neighborNodes.push_back(p->ancestor());
    }

    std::sort(neighborNodes.begin(), neighborNodes.end());
    // 重複要素を末尾に移動し、新しい末尾のイテレータを返す
    auto last = std::unique(neighborNodes.begin(), neighborNodes.end());
    // 重複部分を削除
    neighborNodes.erase(last, neighborNodes.end());
    
    // この粒子 p と連結しているクラスタ代表のID集合を探索
    if (neighborNodes.empty()) {
      StructureNode* newLeaf = new StructureNode(TrackingVector<int>{idx}, p.density);
      nodes.push_back(newLeaf);
      leafindex[idx] = newLeaf;
      
      //currentClusterID++;

      /*for(auto &s : nodes){
	if(!s->children.empty()){
	  for(auto &p : s->children)
	    if(s != p->parent)
	      printf("something wrong, line=%d\n", __LINE__);
	}

	if(s->parent){
	  bool flag = false;
	  for(auto &p : s->parent->children)
	    if(s == p)
	      flag = true;

	  if(flag == false)
	    printf("something wrong, line=%d\n", __LINE__);	    
	}
	}*/
    } else if (neighborNodes.size() == 1) {
      StructureNode* pLeaf = neighborNodes[0];      

      leafindex[idx] = pLeaf;
      pLeaf->add_particle(idx, p.density);
    } else {
      int count = 0;
      TrackingVector<StructureNode*> merger;
      for(size_t i=0;i <  neighborNodes.size();i++){
	StructureNode* adjacent = neighborNodes[i];
	if (adjacent->isLeaf()) {
	  bool flag;
	  if (adjacent->parent)
	    flag = ((adjacent->height() - adjacent->parent->height()) >= minDepth);
	  else
	    flag = ((adjacent->vmax - adjacent->vmin) >= minDepth);

	  if(flag == false || adjacent->indices.size() < static_cast<size_t>(min_npix)){	  
	    merger.push_back(adjacent);
	    continue;
	  }
	}	  
	
	neighborNodes[count] = adjacent;
	count++;
      }
      
      neighborNodes.resize(count);

      if(neighborNodes.empty()){
	neighborNodes.push_back(merger.back());
	merger.pop_back();
      }	

      StructureNode* pLeaf_rep = NULL;
      if (neighborNodes.size() == 1){
	pLeaf_rep = leafindex[idx] = neighborNodes[0];
	pLeaf_rep->add_particle(idx, p.density);
      } else {
	// create a new branch	
	StructureNode* mergeNode = new StructureNode(TrackingVector<int>{idx}, p.density, neighborNodes);
	leafindex[idx] = pLeaf_rep = mergeNode;
	
	nodes.push_back(mergeNode);
      	//currentClusterID++;
      }

      while(!merger.empty()){
	StructureNode *leaf_merged = merger.back();
	merger.pop_back();
	for (int particleID : leaf_merged->indices) 
	  leafindex[particleID] = pLeaf_rep;
	
	pLeaf_rep->merge_node(leaf_merged);
	
	// 親の children コンテナからも leaf_merged を削除
	if (leaf_merged->parent) {
	  auto& siblings = leaf_merged->parent->children;
	  siblings.erase(std::remove(siblings.begin(), siblings.end(), leaf_merged), siblings.end());
	}
	
	nodes.erase(std::remove(nodes.begin(), nodes.end(), leaf_merged), nodes.end());
	delete leaf_merged;
      }
    }    
  }

  // プルーニング対象を順次削除する
  while (true) {
    auto toPrune = pruning::_to_prune(nodes, minDepth, min_npix);
    if (toPrune.empty())
      break;
    for (StructureNode* s : toPrune) {
      StructureNode* parent = s->parent;
      if (!parent)
	continue;
      auto& siblings = parent->children;
      TrackingVector<StructureNode*> merge;
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

  // 親を持たない構造から trunk を再構築
  TrackingVector<StructureNode*>trunk = pruning::_make_trunk(nodes, minDepth, min_npix);
  
  for (size_t i = 0; i < nodes.size(); i++) {    
    StructureNode *sn = nodes[i];
    calc_node_statistic(sn, cloud.pts);
  }

  sortNodesByMass(nodes);    

  for(StructureNode* node : nodes){
    TrackingVector<int> indices_new;
    
    for(size_t i = 0; i < node->indices.size() ; i++){
      int idx = node -> indices[i];
      int original_index = filteredParticles[idx].flag;
      indices_new.push_back(original_index);
    }
    
    node -> indices = indices_new;
  }
  
  return nodes;
}

void FindClump::calc_node_statistic(StructureNode *ns, const TrackingVector<ParticleData>& p){
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
// 1) 質量順にソートする関数（降順: 質量が大きいものが前に来る）
void FindClump::sortNodesByMass(TrackingVector<StructureNode*>& nodes) {
  std::sort(nodes.begin(), nodes.end(), [](const StructureNode* a, const StructureNode* b) {
    return a->totalMass > b->totalMass;
  });
}

//------------------------------------------------------------
// 2) 階層順にソートするための補助関数（pre-order で巡回）
void FindClump::traverseHierarchy(StructureNode* node, TrackingVector<StructureNode*>& sortedNodes) {
  if (!node) return;
  sortedNodes.push_back(node);
  // 子ノードがあれば順次巡回（ここでは順番は children の順序に依存）
  for (StructureNode* child : node->children) {
    traverseHierarchy(child, sortedNodes);
  }
}

// 階層順にソートする関数
// trunk (parent == nullptr) から始め、pre-order で巡回して順序を作成
void FindClump::sortNodesByHierarchy(TrackingVector<StructureNode*>& nodes) {
  TrackingVector<StructureNode*> sortedNodes;
  // nodes 内の各ノードから、親が nullptr であるもの（trunk）を対象に巡回
  for (StructureNode* node : nodes) {
    if (node->parent == nullptr) {
      traverseHierarchy(node, sortedNodes);
    }
  }
  nodes = sortedNodes;
}


// フィルタリング処理の例
TrackingVector<ParticleData> FindClump::filterParticles(const TrackingVector<ParticleData>& particles, double threshold, const std::string &var) const{
  TrackingVector<ParticleData> filtered;  
  for (size_t i = 0; i < particles.size(); ++i) {
      const ParticleData &p = particles[i];
      if(p.type >= 3){
	ParticleData copy = p;  // Particle をコピー
	copy.flag = static_cast<int>(i);  // 元のインデックスを保存
	filtered.push_back(copy);
      }
      
      if (p.getValue(var) >= threshold) {
	ParticleData copy = p;  // Particle をコピー
	copy.flag = static_cast<int>(i);  // 元のインデックスを保存
	filtered.push_back(copy);
      }
  }
  return filtered;
}

TrackingVector<ParticleData> FindClump::getAllChildren(StructureNode* node, TrackingVector<ParticleData>& p) const{
  TrackingVector<ParticleData> pts;
  if (!node)
    return pts;

  // 現在のノードの indices を追加
  for (int idx : node->indices) {
    pts.push_back(p[idx]);
  }

  // 子ノードに対して再帰的に処理し、得られた indices を結合
  for (StructureNode* child : node->children) {
    TrackingVector<ParticleData> childIndices = getAllChildren(child, p);
    pts.insert(pts.end(), childIndices.begin(), childIndices.end());
  }

  return pts;
}



void FindClump::ShowFindClumpsUI(TrackingVector<ParticleData>& originalParticles, const HeaderInfo& header){
  if(!showWindowClumpFinder) return;    

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);  
  ImGui::Begin("clump finder", &showWindowClumpFinder, ImGuiWindowFlags_None);

  const char* quantities[] = { "Density", "Temperature", "val", "val2", "Hsml" };
  // 各軸に使う変数のインデックス（デフォルトでは X 軸に "x"、Y 軸に "y" を選択）
  static int selectedVar = 0;
  
  ImGui::Combo("Quantity", &selectedVar, quantities, IM_ARRAYSIZE(quantities));  
  std::string var = quantities[selectedVar];
  
  ImGui::InputFloat("Density Threshold", &densityThreshold, 0.0, 1.0);
  ImGui::InputInt("minParticles", &minParticles, 10, 10);
  ImGui::InputFloat("minDepth", &minDepth, 10, 10);
  
  ImGui::Checkbox("Linking Length using Hsml or cell size", &useHsml);  

  if (!useHsml){
    ImGui::InputFloat("Linking Length", &linkingLength, 0.01, 0.1);
  }else{
    ImGui::InputFloat("Linking Length over cell size", &linkingLength_over_cell_size, 0.01, 0.1);
  }      

  if (ImGui::Button("Find Clumps")) {
    if(findClumpComputed == true)
      for(auto node : nodeList)
	delete node;

    flagDendrogramComputed = false;
    
    nodeList = findClumps(originalParticles, minParticles, var);      

#ifdef USE_CONVEX_HULL
    showHull.assign(nodeList.size(), false);
#endif
    
    findClumpComputed = true;
    flagFOFComputed = true;
    flagClearCache = true;
  }

  ImGui::SameLine();
  if (ImGui::Button("Dendrogram")) {
    if(findClumpComputed == true)
      for(auto node : nodeList)
	delete node;
    
    flagDendrogramComputed = false;
    
    nodeList = findClumpsDendrogram(originalParticles, minParticles, var);

#ifdef USE_CONVEX_HULL
    showHull.assign(nodeList.size(), false);
#endif
    
    findClumpComputed = true;
    flagFOFComputed = false;
    flagDendrogramComputed = true;
    flagClearCache = true;
  }

#ifdef CLUMP_DATA_READ
  static char buf[255] = "clumpList.hdf5";
  ImGui::InputText("output file name", buf, IM_ARRAYSIZE(buf));    

  char fdir[256];
  strncpy(fdir, gFileInfo->folderPath, sizeof(fdir)-1);
  int snapshotIndex = gFileInfo->currentFileIndex;

  char temp[513];
  snprintf(temp, sizeof(temp), "%s/%s", fdir, buf);
  
  if(ImGui::Button("Output clump data")){
    std::string filename(temp);
    writeFOFtoHDF5(originalParticles, header, filename, snapshotIndex);
  }
#endif
  
  if(ImGui::CollapsingHeader("Clump Lists")){
    static float minPeakDensity = 1.e4;
    ImGui::InputFloat("minimum peak density", &minPeakDensity);
  
    static bool flagShowLeaves = false;
    ImGui::Checkbox("Leaves", &flagShowLeaves);    

    ImGui::SameLine();
    enum class SortMode { Mass, Hierarchy };
  
    // static 変数で現在のソートモードを保持
    static SortMode currentSortMode = SortMode::Mass;  
    // ラジオボタンで「質量順」を選択
    if (ImGui::RadioButton("Sort by Mass", currentSortMode == SortMode::Mass)) {
      if (currentSortMode != SortMode::Mass) {
	currentSortMode = SortMode::Mass;
	if(flagDendrogramComputed){
	  sortNodesByMass(nodeList);

	  flagClearCache = true;
	}
      }
    }
  
    ImGui::SameLine();
    // ラジオボタンで「階層順」を選択
    if (ImGui::RadioButton("Sort by Hierarchy", currentSortMode == SortMode::Hierarchy)) {
      if (currentSortMode != SortMode::Hierarchy) {
	currentSortMode = SortMode::Hierarchy;
	if(flagDendrogramComputed){
	  sortNodesByHierarchy(nodeList);

	  flagClearCache = true;
	}
      }
    }

    if(findClumpComputed){
      if (ImGui::BeginTable("ClumpTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
	ImGui::TableSetupColumn("Clump Info", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("Hull", ImGuiTableColumnFlags_WidthFixed, 50.0f);
	ImGui::TableHeadersRow();

#ifdef USE_CONVEX_HULL
	bool flag_button_pushed = false;
#endif
	
	// 各クランプ情報を表示
	for (size_t i = 0; i < nodeList.size(); i++) {
	  int count;
	  double mass, pos[3], radius;

	  const StructureNode *node = nodeList[i];
	  
	  if(node->vpeak < minPeakDensity)
	    continue;
	  
	  if(flagShowLeaves)
	    if(!node->isLeaf())
	      continue;
	  
	  count = node->count;
	  mass = node->totalMass;
	  pos[0] = node->pos_cm[0];
	  pos[1] = node->pos_cm[1];
	  pos[2] = node->pos_cm[2];
	  radius = 0.; // to be constructed
	  
	  ImGui::TableNextRow();
            
	  // 左側の列：clump の情報をSelectableで表示
	  ImGui::TableSetColumnIndex(0);
	  char label[256];
	  snprintf(label, sizeof(label), "%4ld   %4d    %g    (%.3f, %.3f, %.3f) %g",
		   i, count, mass, pos[0], pos[1], pos[2], radius);
	  if (ImGui::Selectable(label, false)) {
	    // クリックされたらカメラをクランプの重心に移動
	    float dist = glm::length(camCtx.cameraPos - camCtx.cameraTarget);
	    glm::vec3 direction = camCtx.cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
	    camCtx.cameraTarget = glm::vec3(pos[0], pos[1], pos[2]);
	    camCtx.cameraPos = camCtx.cameraTarget - direction * dist;
	  }

#ifdef USE_CONVEX_HULL
	  // 右側の列：チェックボックス
	  ImGui::TableSetColumnIndex(1);
	  char checkboxLabel[64];
	  snprintf(checkboxLabel, sizeof(checkboxLabel), "Hull##%ld", i);
	  // vector<bool> は特殊化しているため、一時変数を使って更新する
	  bool flag = showHull[i];
	  if (ImGui::Checkbox(checkboxLabel, &flag)){
	    if(flag != showHull[i]){
	      flag_button_pushed = true;	    
	      //convexHullDirtyMap[i] = true;
	    }	    
	    showHull[i] = flag;
	  }
#endif
	}

#ifdef USE_CONVEX_HULL
	if(flag_button_pushed){
	  for(auto &p : originalParticles)
	    p.flag = 0;

	  for(size_t i=0;i<showHull.size();i++){
	    if(showHull[i] == 0)
	      continue;
	    
	    setFlagsRecursively(nodeList[i], originalParticles);	  
	  }

	  flag_button_pushed = false;
	}
#endif
      
	ImGui::EndTable();
      }
    }
  }

  if(ImGui::CollapsingHeader("Clump Mass Histogram")){
    static bool histogramComputed = false;
    static TrackingVector<float> mass_array;

    // 自動レンジを使うかどうかのチェックボックス
    static bool autoRange = true;
    ImGui::Checkbox("Auto Range", &autoRange);
  
    // 手動レンジ入力用（autoRange==false の場合）
    static float range1_min = 0.0f, range1_max = 1.0f;
    if (!autoRange)
      {
	ImGui::InputFloat("X Axis Min", &range1_min, 0.0f, 0.0f, "%g");
	ImGui::InputFloat("X Axis Max", &range1_max, 0.0f, 0.0f, "%g");
      }

    static bool histogramLogScaleX = true;
    static bool histogramLogScaleY = true;  
    ImGui::Checkbox("Use Log scale X", &histogramLogScaleX);

    ImGui::SameLine();
    ImGui::Checkbox("Use Log scale Y", &histogramLogScaleY);

    // ビン数の入力
    static int bins = 50;
    ImGui::InputInt("Number of bins", &bins);

    static bool bins_auto = false;
    ImGui::SameLine();
    ImGui::Checkbox("Use Log scale Y", &bins_auto);

    if (ImGui::Button("Compute 1D Histogram")) {
      mass_array = {};

      // 各クランプ情報を表示
      for (size_t i = 0; i < nodeList.size(); i++) {
	const StructureNode *node = nodeList[i];
	float mass = node->totalMass;
	  
	if(histogramLogScaleX)
	  mass = log10(mass);
      
	mass_array.push_back(mass);
      }
        
      float massMin = std::numeric_limits<float>::max();
      float massMax = std::numeric_limits<float>::lowest();
      for (const auto &mass : mass_array) {      
	massMin = std::min(massMin, mass);
	massMax = std::max(massMax, mass);
      }
    
      if (massMin == massMax)
	massMax = massMin + 1.0f;

      if (autoRange){
	range1_min = massMin;
	range1_max = massMax;
      }
      
      histogramComputed = true;
    }

    if(histogramComputed){
      if (ImPlot::BeginPlot("Mass Histogram", ImVec2(-1,300))) {
	if (histogramLogScaleY)
	  ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
	else
	  ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Linear);
	
	ImPlot::SetupAxisLimits(ImAxis_X1, range1_min, range1_max, ImGuiCond_Always);

	int bins_plot = bins;
	if(bins_auto)
	  bins_plot = -1;
	
	ImPlot::PlotHistogram("Mass", mass_array.data(), static_cast<int>(mass_array.size()), bins_plot);
	ImPlot::EndPlot();
      }
    }
  }
  
  ImGui::End();
}


#ifdef CLUMP_DATA_READ
void FindClump::do_FOF_and_output_clump_data(int method, TrackingVector<ParticleData>&particles, const HeaderInfo& header, char *filename, int snapshotIndex){
  char var_name[]="Density";
  std::string var(var_name);
  
  if(method == 0)
    nodeList = findClumps(particles, minParticles, var);
  else
    nodeList = findClumpsDendrogram(particles, minParticles, var);

  for(auto& node : nodeList)
    node->construct_ID_array(particles);

  std::string fname(filename);
  writeFOFtoHDF5(particles, header, fname, snapshotIndex);  

  if(nodeList_prev.size() > 0){
    if(nodeList_prev.size())
      findClumpsInNextSnapshot(particles);
    
    addNextClumpIDtoHDF5(nodeList_prev, fname, snapshotIndex_prev);
  }
    
  nodeList_prev = nodeList;
  snapshotIndex_prev = snapshotIndex;
}


void FindClump::findClumpsInNextSnapshot(TrackingVector<ParticleData>&particles){
  struct ParticleInfo {
    int ID;
    int clumpID;
  };

  TrackingVector<struct ParticleInfo> particleIDs;

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

    TrackingVector<int> counts(nClump, 0);
    
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



void FindClump::writeFOFtoHDF5(const TrackingVector<ParticleData>& particles, const HeaderInfo& header,
			       const std::string &filename,
			       int snapshotIndex)
{
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
  out.time = header.time;
  
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
      int pid = particles[idx].ID;
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
	position[k] += particles[idx].mass * particles[idx].original_pos[k];
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
			      TrackingVector<int> &sorted_particle_id,
			      TrackingVector<int> &clump_id,
			      TrackingVector<int> &clump_offset,
			      TrackingVector<int> &clump_size
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
