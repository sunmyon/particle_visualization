#include "data/spatial/particle_octree.h"
#include <algorithm>
#include <cstdio>
#include <unordered_set>

// ParticleOctree constructor.
ParticleOctree::ParticleOctree(std::vector<SimulationElementForTree>&& all,
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

ParticleOctree::~ParticleOctree() = default;


// Internal helper: recursively generate a Node.
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
				     std::vector<SimulationElementForTree>& particles,
                                     size_t                               minParticles,
                                     size_t                               maxDepth,
                                     size_t                               depth,
				     float                                isoLevel,
				     bool                                 isIsoDensity,
				     bool                                 force)
{
  if(!force && isIsoDensity){
    float minValue = 0.0f;
    float maxValue = 0.0f;
    computeValueRange(particles.data() + start, count, minValue, maxValue);
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
  
  // 1. Compute child boxes.
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

  // 2. Count particles first as a dry run.
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

  // 3. If any child would fall below minParticles, keep this node as a leaf.
  /*if(!force){
    for (size_t c : childCounts) {
      if (c > 0 && c < minParticles) {
	// Ignore empty child boxes; c==0 is allowed.
	return;  // Keep isLeaf=true.
      }
    }
    }*/
  
  // Also enforce the depth limit.
  if (depth >= maxDepth) return;

  // 4. Actually subdivide.
  isLeaf = false;
  
  for (int i = 0; i < 8; ++i) {
    auto c = std::make_unique<Node>(childBoxes[i], 0, 0, depth+1, this, uint8_t(i));
    children[i] = std::move(c);
  }

  // 5. In-place partitioning.
  auto beginIt = itBegin;
  auto endIt   = itEnd;
  size_t offset = start;
  for (int i = 0; i < 8; ++i) {
    const BoundingBox& cb = childBoxes[i];
    auto it = std::partition(beginIt, endIt,
			     [&](const SimulationElementForTree& p){ return cb.contains(p.pos); });
    size_t newCount = std::distance(beginIt, it);
    children[i]->start = offset;
    children[i]->count = newCount;
    offset += newCount;
    beginIt = it;
  }

  // 6. Recursively subdivide each child node.
  for (auto& child : children) {
    if (child->count >= minParticles) {
      child->subdivide(tree, particles, minParticles, maxDepth, depth + 1, isoLevel, isIsoDensity);
    }
  }
}

//#define TEST1 1

void ParticleOctree::balanceTree(bool isIsoDensity)
{
  while (true) {
    std::vector<Node*> leaves;
    collectLeaves(root_.get(), leaves);

    std::vector<Node*> toRefine;
    std::unordered_set<Node*> queued;

    for (auto leaf : leaves) {
      for (int dir = 0; dir < 6; ++dir) {
	auto neighbors = findAllNeighbors(leaf, dir);
	if (neighbors.empty()) continue;

	for (Node* nb : neighbors) {
	  int depthGap = std::abs(int(nb->depth) - int(leaf->depth));
	  if (depthGap < 2) continue;  // OK

	  Node* shallower = (leaf->depth < nb->depth) ? leaf : nb;
	  if (!shallower->isLeaf || shallower->depth >= maxDepth_) continue;

	  if (queued.insert(shallower).second) {
	    toRefine.push_back(shallower);
	  }
	}
      }
    }

    if (toRefine.empty()) break;

    for (Node* node : toRefine) {
      if (!node->isLeaf) continue;
      node->subdivide(*this,
		      particles_,
		      minParticles_,
		      maxDepth_,
		      node->depth,
		      isoLevel_,
		      isIsoDensity,
		      /*force=*/true);
    }
  }
}

// -------------- Particle spatial tree helpers ----------------
namespace {

// Return whether boxes share a face in direction dir (0..5).
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

} // anonymous namespace

// Neighbor search by scanning all leaves.
ParticleOctree::Node*
ParticleOctree::findNeighbor(Node* /*unused*/,
                             const BoundingBox& box,
                             int dir)
{
    // 1. Collect the current leaves.
    std::vector<Node*> leaves;
    collectLeaves(root_.get(), leaves);

    Node* best = nullptr;
    for (Node* n : leaves) {
        if (touchesFace(box, n->box, dir)) {
            if (!best || n->depth > best->depth) best = n; // Deepest leaf.
        }
    }
    return best;     // nullptr if no neighbor was found.
}

inline int siblingIndex(int dir, int myIdx) {
    // The child index stores bits (bx, by, bz) = (myIdx&1, (myIdx>>1)&1, (myIdx>>2)&1).
    // axisBits = 0 for X, 1 for Y, 2 for Z
    const int axis = (dir/2);             // 0,1,2
    const int mask = 1 << axis;           // Bit mask to flip.
    return myIdx ^ mask;                  // Flipping that bit gives the sibling index.
}

std::vector<ParticleOctree::Node*>
ParticleOctree::findAllNeighbors(const Node* leaf, int dir) const {
  std::vector<Node*> result;
  const Node* cur = leaf;

  // Walk up the parent chain and inspect sibling subtrees at each level.
  while (cur->parent) {
    uint8_t myIdx  = cur->childIdx;
    uint8_t sibIdx = siblingIndex(dir, myIdx);
    Node* sib = cur->parent->children[sibIdx].get();
    if (sib) {
      // Collect all face-touching leaves below that sibling subtree.
      collectFaceLeaves(sib, dir, leaf->box, result);
    }
    cur = cur->parent;
  }

  return result;
}

// Add all leaves in the sibling subtree that share a face with origBox in direction dir.
void ParticleOctree::collectFaceLeaves(Node* sib,
				       int          dir,
				       const BoundingBox& origBox,
				       std::vector<Node*>& out) const
{
  if (sib->isLeaf) {
    if (touchesFace(sib->box, origBox, dir)) 
      out.push_back(sib);
    return;
  }

  const int axis = dir / 2;              // 0=X,1=Y,2=Z
  const int want = (dir % 2 == 0 ? 1 : 0);// Positive directions use bit=0; negative directions use bit=1.

  // Restrict the search to the four relevant children.
  for (uint8_t ci = 0; ci < 8; ++ci) {
    if (((ci >> axis) & 1) != want) 
      continue;              // Skip children on the opposite side.
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
                                 std::vector<const SimulationElementForTree*>& out) const
{
    out.clear();
    querySphereRecursive(root_.get(), center, radius*radius, out);
}

// ------------- private -------------

void ParticleOctree::querySphereRecursive(
        const Node*                     node,
        const glm::vec3&                center,
        float                           radius2,
        std::vector<const SimulationElementForTree*>& out) const
{
  if (!node) return;

  // Stop if the AABB and sphere do not intersect.
  if (sqDistPointAABB(center, node->box) > radius2) return;

  if (node->isLeaf) {
    // Check particles directly.
    const SimulationElementForTree* base = &particles_[node->start];
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
  // Fallback to the current node if no child contains the point; this should not happen.
  printf("why?? we could not find the leaf conataining point p=%g %g %g\n", p.x, p.y, p.z);

  return node;
}
