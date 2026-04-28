// ParticleOctree.cpp
#include "OctTree/ParticleOctree.h"
#include "analysis/isosurface/density_evaluate.h"
#include <algorithm>
#include "analysis/isosurface/marching_cubes.h"

// ——— ParticleOctree のコンストラクタ ———
ParticleOctree::ParticleOctree(TrackingVector<ParticleDataForTree>&& all,
                               const BoundingBox&                    worldBox,
                               size_t                                minParticles,
                               size_t                                maxDepth,
			       float                                 isoLevel,
			       bool                                  isIsoDensity)
  : particles_(std::move(all))
  , isoLevel_(isoLevel)
  , minParticles_(minParticles)
  , maxDepth_(maxDepth)
{
  root_ = std::make_unique<Node>(worldBox, 0, particles_.size(), 0, nullptr, 0);  
  root_->subdivide(*this, particles_, minParticles_, maxDepth_, isoLevel_, isIsoDensity, 0);
}


// ——— 内部：Node を再帰生成 ——
std::unique_ptr<ParticleOctree::Node>
ParticleOctree::buildNode(const BoundingBox& box,
                          size_t             start,
                          size_t             count,
                          size_t             depth,
			  bool               isIsoDensity)
{
  auto node = std::make_unique<Node>(box, start, count, 0, nullptr, 0);  
  node->subdivide(*this, particles_, minParticles_, maxDepth_, depth, isoLevel_, isIsoDensity);

  return node;
}


void ParticleOctree::Node::subdivide(ParticleOctree&           tree,  
				     TrackingVector<ParticleDataForTree>& particles,
                                     size_t                               minParticles,
                                     size_t                               maxDepth,
                                     size_t                               depth,
				     float                                isoLevel,
				     bool                                 isIsoDensity,
				     bool                                 force)
{
  if (force)
    std::cout<<"[force] depth="<<depth<<" start="<<start<<" cnt="<<count<<'\n';
  
  if(!force && isIsoDensity){
    computeValueRange(particles.data() + start,  count, minValue, maxValue);    
    if (maxValue < isoLevel || minValue > isoLevel){
      isLeaf = true;
      return;
    }
  }

  if(!force){
    if (count < minParticles){
      isLeaf = true;
      return;
    }
  }
  
  // 1) 子ボックスを計算
  glm::vec3 center = (box.min + box.max) * 0.5f;
  std::array<BoundingBox,8> childBoxes;
  for (int i = 0; i < 8; ++i) {
    glm::vec3 newMin{
      (i & 1) ? center.x : box.min.x,
      (i & 2) ? center.y : box.min.y,
      (i & 4) ? center.z : box.min.z
    };
    glm::vec3 newMax{
      (i & 1) ? box.max.x : center.x,
      (i & 2) ? box.max.y : center.y,
      (i & 4) ? box.max.z : center.z
    };
    childBoxes[i] = BoundingBox{newMin, newMax};
  }

  // 2) まずは粒子を数えてみる（dry-run）
  std::array<size_t,8> childCounts = {0,0,0,0,0,0,0,0};
  auto itBegin = particles.begin() + start;
  auto itEnd   = itBegin + count;
  for (auto it = itBegin; it != itEnd; ++it) {
    for (int i = 0; i < 8; ++i) {
      if (childBoxes[i].contains(it->pos)) {
	++childCounts[i];
	break;
      }
    }
  }

  // 3) 子ノードのいずれかが minParticles 未満なら、分割せず葉に留める
  /*if(!force){
    for (size_t c : childCounts) {
      if (c > 0 && c < minParticles) {
	// （c==0 の空ボックスは無視；0でも OK）
	return;  // isLeaf=true のまま
      }
    }
    }*/
  
  // 深さ制限も確認
  if (depth >= maxDepth) return;

  // 4) 本当に subdivide する
  isLeaf = false;
  
  for (int i = 0; i < 8; ++i) {
    auto c = std::make_unique<Node>(childBoxes[i], 0, 0, depth+1, this, uint8_t(i));
    children[i] = std::move(c);
  }

  // 5) in-place パーティショニング
  auto beginIt = itBegin;
  auto endIt   = itEnd;
  size_t offset = start;
  for (int i = 0; i < 8; ++i) {
    const BoundingBox& cb = childBoxes[i];
    auto it = std::partition(beginIt, endIt,
			     [&](const ParticleDataForTree& p){ return cb.contains(p.pos); });
    size_t newCount = std::distance(beginIt, it);
    children[i]->start = offset;
    children[i]->count = newCount;
    offset += newCount;
    beginIt = it;
  }

  // 6) 各子ノードを再帰的に subdivide
  for (auto& child : children) {
    if (child->count >= minParticles) {
      child->subdivide(tree, particles, minParticles, maxDepth, depth + 1, isoLevel, isIsoDensity);
    }
  }
}

//#define TEST1 1

void ParticleOctree::balanceTree(bool isIsoDensity)
{
  bool didRefine = true;
  int iloop=0;

#ifdef TEST1
  TrackingVector<Node*> leaves;
  collectLeaves(root_.get(), leaves);

  std::vector<Node*> current;
  for (auto leaf : leaves){
    bool flag_violate = false;
    for (int dir = 0; dir < 6; ++dir) {
      auto neighbors = findAllNeighbors(leaf, dir);
      if (neighbors.empty()) continue;

      for (Node* nb : neighbors) {
	int depthGap = std::abs(int(nb->depth) - int(leaf->depth));
	if (depthGap < 2)
	  continue;  // OK
	
	flag_violate = true;
	break;
      }      
    }
    
    if (flag_violate)
      current.push_back(leaf);
  }

  std::vector<Node*> next; 
  while (!current.empty()) {
    next.clear();
#pragma omp parallel
    {
      // スレッドローカルバッファ
      std::vector<Node*> local_next;
      local_next.reserve(current.size());
	
#pragma omp for schedule(dynamic)
      for (int i = 0; i < (int)current.size(); ++i) {
	Node* leaf = current[i];

	if (!leaf->isLeaf)
	  continue;

	bool leafWasSubdivided = false;
	for (int dir = 0; dir < 6 && !leafWasSubdivided; ++dir) {
	  for (Node* nb : findAllNeighbors(leaf, dir)) {
	    int gap = std::abs(int(nb->depth) - int(leaf->depth));
	    if (gap < 2) 
	      continue;
	    
	    // 差が2以上 → subdivide
	    Node* shallower = (leaf->depth < nb->depth) ? leaf : nb;
	    
	    if (shallower->try_mark_subdivided()) {
#pragma omp critical
	      {
		shallower->subdivide(*this,
				     particles_,
				     minParticles_,
				     maxDepth_,
				     shallower->depth,
				     isoLevel_,
				     isIsoDensity,
				     /*force=*/true);
	      }
	    }
	    // subdivide 波及チェック用にキューへ
	    for (const auto& uptr : shallower->children) {
	      Node* c = uptr.get();      // unique_ptr<Node> → Node*
	      if (c)                     // nullptr チェック（念のため）
		local_next.push_back(c);
	    }
	    
	    // もう一方のセルも enqueue
	    Node* other = (shallower == leaf ? nb : leaf);
	    if (other->isLeaf)
	      local_next.push_back(other);
	    
	    // leaf 自身を subdivide した場合だけ後続チェック不要
	    if (shallower == leaf) {
	      leafWasSubdivided = true;
	      break;
	    }
	  }

	}
      }
      // スレッドローカル → グローバル next へマージ
#pragma omp critical
      next.insert(next.end(), local_next.begin(), local_next.end());
    }
    current.swap(next);
  }
#else
  while (didRefine) {
    didRefine = false;
    printf("loop%d\n", iloop++);
	
    // ① 毎パス最新の葉リストを取り直す
    TrackingVector<Node*> leaves;
    collectLeaves(root_.get(), leaves);
	
    // ② 2-to-1 ルールを破るペアを探す
    for (auto leaf : leaves) {
      for (int dir = 0; dir < 6; ++dir) {
	auto neighbors = findAllNeighbors(leaf, dir);
	if (neighbors.empty()) continue;

	// ③ 面接触するすべての隣とチェック
	for (Node* nb : neighbors) {
	  int depthGap = std::abs(int(nb->depth) - int(leaf->depth));
	  if (depthGap < 2) continue;  // OK

	  // ギャップ発見！
	  std::cout << "[gap] leafDepth=" << leaf->depth
		    << " nbDepth="  << nb->depth
		    << " leafCnt="  << leaf->count
		    << " nbCnt="    << nb->count << '\n';

	  // 浅いほうを強制 subdivide
	  Node* shallower = (leaf->depth < nb->depth) ? leaf : nb;
	  std::cout << "[refine] node@" << shallower
		    << " depth=" << shallower->depth
		    << " cnt="   << shallower->count << '\n';

	  shallower->subdivide(*this,
			       particles_,
			       minParticles_,
			       maxDepth_,
			       shallower->depth,
			       isoLevel_,
			       isIsoDensity,
			       /*force=*/true);

	  didRefine = true;
	  break;  // この方向はもう OK
	}
	
	if (didRefine) break;
      }
      if (didRefine) break;
    }
  }
#endif
}

// -------------- ParticleOctree.cpp ----------------
namespace {

// 方向 dir (0..5) で face を共有するか
bool touchesFace(const BoundingBox& a, const BoundingBox& b, int dir)
{
    const float eps = 1e-7f;
    auto overlap = [&](float a0,float a1,float b0,float b1){
        return a1 > b0 + eps && b1 > a0 + eps;
    };
    switch (dir) {
        case 0: return std::abs(a.max.x - b.min.x) < eps &&
                       overlap(a.min.y,a.max.y,b.min.y,b.max.y) &&
                       overlap(a.min.z,a.max.z,b.min.z,b.max.z);
        case 1: return std::abs(a.min.x - b.max.x) < eps &&
                       overlap(a.min.y,a.max.y,b.min.y,b.max.y) &&
                       overlap(a.min.z,a.max.z,b.min.z,b.max.z);
        case 2: return std::abs(a.max.y - b.min.y) < eps &&
                       overlap(a.min.x,a.max.x,b.min.x,b.max.x) &&
                       overlap(a.min.z,a.max.z,b.min.z,b.max.z);
        case 3: return std::abs(a.min.y - b.max.y) < eps &&
                       overlap(a.min.x,a.max.x,b.min.x,b.max.x) &&
                       overlap(a.min.z,a.max.z,b.min.z,b.max.z);
        case 4: return std::abs(a.max.z - b.min.z) < eps &&
                       overlap(a.min.x,a.max.x,b.min.x,b.max.x) &&
                       overlap(a.min.y,a.max.y,b.min.y,b.max.y);
        case 5: return std::abs(a.min.z - b.max.z) < eps &&
                       overlap(a.min.x,a.max.x,b.min.x,b.max.x) &&
                       overlap(a.min.y,a.max.y,b.min.y,b.max.y);
    }
    return false;
}

} // ── anonymous ns

// ------------ 全葉スキャン版 findNeighbor -------------
ParticleOctree::Node*
ParticleOctree::findNeighbor(Node* /*unused*/,
                             const BoundingBox& box,
                             int dir)
{
    // 1) いまの全 leaf を一覧
    TrackingVector<Node*> leaves;
    collectLeaves(root_.get(), leaves);

    Node* best = nullptr;
    for (Node* n : leaves) {
        if (touchesFace(box, n->box, dir)) {
            if (!best || n->depth > best->depth) best = n; // 最深 leaf
        }
    }
    return best;     // 無ければ nullptr
}

inline int siblingIndex(int dir, int myIdx) {
    // child インデックスはビット構成 (bx,by,bz) = (myIdx&1, (myIdx>>1)&1, (myIdx>>2)&1)
    // axisBits = 0 for X, 1 for Y, 2 for Z
    const int axis = (dir/2);             // 0,1,2
    const int mask = 1 << axis;           // flip したいビットマスク
    return myIdx ^ mask;                  // ビットを反転させたインデックスが兄弟
}

TrackingVector<ParticleOctree::Node*>
ParticleOctree::findAllNeighbors(const Node* leaf, int dir) const {
  TrackingVector<Node*> result;
  const Node* cur = leaf;

  // 親をたどって、各レベルで兄弟サブツリーを探す
  while (cur->parent) {
    uint8_t myIdx  = cur->childIdx;
    uint8_t sibIdx = siblingIndex(dir, myIdx);
    Node* sib = cur->parent->children[sibIdx].get();
    if (sib) {
      // その兄弟配下から面接触するすべての葉を集める
      collectFaceLeaves(sib, dir, leaf->box, result);
    }
    cur = cur->parent;
  }

  return result;
}

// dir 方向の面を origBox と共有する sib サブツリー配下のすべての葉を out に追加
void ParticleOctree::collectFaceLeaves(Node* sib,
				       int          dir,
				       const BoundingBox& origBox,
				       TrackingVector<Node*>& out) const
{
  if (sib->isLeaf) {
    if (touchesFace(sib->box, origBox, dir)) 
      out.push_back(sib);
    return;
  }

  const int axis = dir / 2;              // 0=X,1=Y,2=Z
  const int want = (dir % 2 == 0 ? 1 : 0);// + 方向は bit=0, - 方向は bit=1

  // 子を４つに絞ってチェック
  for (uint8_t ci = 0; ci < 8; ++ci) {
    if (((ci >> axis) & 1) != want) 
      continue;              // 反対側の子は見に行かない
    auto* c = sib->children[ci].get();
    if (!c) continue;
    if (touchesFace(c->box, origBox, dir)) {
      collectFaceLeaves(c, dir, origBox, out);
    }
  }
}

namespace {
  inline float sqDistPointAABB(const glm::vec3& p, const BoundingBox& b)
  {
    float dx = std::max(std::max(b.min.x - p.x, 0.0f), p.x - b.max.x);
    float dy = std::max(std::max(b.min.y - p.y, 0.0f), p.y - b.max.y);
    float dz = std::max(std::max(b.min.z - p.z, 0.0f), p.z - b.max.z);
    return dx*dx + dy*dy + dz*dz;
  }  
} 

// ------------- public -------------
void ParticleOctree::querySphere(const glm::vec3& center,
                                 float            radius,
                                 TrackingVector<const ParticleDataForTree*>& out) const
{
    out.clear();
    querySphereRecursive(root_.get(), center, radius*radius, out);
}

// ------------- private -------------

void ParticleOctree::querySphereRecursive(
        const Node*                     node,
        const glm::vec3&                center,
        float                           radius2,
        TrackingVector<const ParticleDataForTree*>& out) const
{
  if (!node) return;

  // AABB と球が交差しなければ探索打ち切り
  if (sqDistPointAABB(center, node->box) > radius2) return;

  if (node->isLeaf) {
    // 粒子を直接チェック
    const ParticleDataForTree* base = &particles_[node->start];
    for (size_t i = 0; i < node->count; ++i) {
      const auto& p = base[i];
      float r2 = glm::dot(p.pos - center, p.pos - center);
      if (r2 <= radius2)
	out.push_back(&p);
    }
  } else {
    for (const auto& c : node->children)
      querySphereRecursive(c.get(), center, radius2, out);
  }
}

static void printRangesRecursive(const ParticleOctree::Node* n, int indent = 0)
{
    if (!n) return;
    std::string pad(indent * 2, ' ');
    std::cout << pad << "depth=" << n->depth
              << "  min=" << n->minValue
              << "  max=" << n->maxValue << '\n';

    if (!n->isLeaf) {
        for (const auto& c : n->children)
            printRangesRecursive(c.get(), indent + 1);
    }
}

void ParticleOctree::debugPrintRanges() const
{
    std::cout << "=== Octree value ranges ===\n";
    printRangesRecursive(root_.get());
    std::cout << "===========================\n";
}


void ParticleOctree::evaluateEdgeValueForAllLeaves(){
  TrackingVector<ParticleDataForKdTree> particles;
  particles.reserve(particles_.size());
  
  for(auto& pc : particles_){
    ParticleDataForKdTree p;
    p.pos = pc.pos;
    p.val = pc.val;

    particles.push_back(p);
  }

  SPHInterpolator sph(std::move(particles));
  
  const std::array<glm::vec3, 8> cubeOffsets = {
    glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(1, 1, 0), glm::vec3(0, 1, 0),
    glm::vec3(0, 0, 1), glm::vec3(1, 0, 1), glm::vec3(1, 1, 1), glm::vec3(0, 1, 1)
  };
  
  auto leaves = getAllLeafNodes();    
  for (auto& leaf : leaves) {
    for (int i=0;i<8;i++) {
      auto offs = cubeOffsets[i];
	
      glm::vec3 extents = leaf->box.max - leaf->box.min;
      glm::vec3 samplePos = leaf->box.min + offs * extents;
      
      float v = sph.sample(samplePos);
      leaf->edgeValues[i] = v;
    }
  }    
}

bool ParticleOctree::contains(const ParticleOctree::Node* n, const glm::vec3 &p) {
  return (p.x >= n->box.min.x && p.x <= n->box.max.x &&
          p.y >= n->box.min.y && p.y <= n->box.max.y &&
          p.z >= n->box.min.z && p.z <= n->box.max.z);
}

const ParticleOctree::Node* ParticleOctree::findLeafContainingRoot(const glm::vec3 &p)
{
  return findLeafContaining(root_.get(), p);
}

const ParticleOctree::Node* ParticleOctree::findLeafContaining(const ParticleOctree::Node* node, const glm::vec3 &p)
{
  if (node->isLeaf) return node;
  for (unsigned i = 0; i < 8; ++i) {
    const auto* c = node->children[i].get();
    if (contains(c, p))
      return findLeafContaining(c, p);
  }
  // 万一どこにも入らなければ root を返す（理想的には起きない）
  printf("why?? we could not find the leaf conataining point p=%g %g %g\n", p.x, p.y, p.z);

  return node;
}
