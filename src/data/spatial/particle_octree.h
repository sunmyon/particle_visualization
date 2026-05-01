#pragma once
#include <vector>
#include <memory>
#include <array>
#include <glm/glm.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>
#include "data/spatial/spatial_tree_types.h"

/// Own all particles centrally; octree nodes store only index ranges.
class ParticleOctree {
public:
  /// Move particles in and build a root node covering the full worldBox.
  ParticleOctree(std::vector<ParticleDataForTree>&& allParticles,
		 const BoundingBox&             worldBox,
		 size_t                         minParticles = 8,
		 size_t                         maxDepth     = 20,
		 float                          isoLevel     = 0.,
		 bool                           isIsoDensity = false);
  ~ParticleOctree();

  /// Node structure storing leaf/internal state and partition metadata.
  struct Node {
    BoundingBox                             box;       ///< Spatial range for this node.
    size_t                                  start, count;     ///< Particle count.
    size_t                                  depth;
    std::array<std::unique_ptr<Node>, 8>    children;  ///< Child node pointers.
    bool                                    isLeaf;    ///< Whether this node is a leaf.

    std::atomic_flag subdivided_flag = ATOMIC_FLAG_INIT;

    bool try_mark_subdivided() {
      return !subdivided_flag.test_and_set(std::memory_order_acquire);
    }
    
    Node*     parent    = nullptr;  // Parent pointer.
    uint8_t   childIdx  = 0;        // Child index within the parent (0..7).
    Node(const BoundingBox& b, size_t s, size_t c, size_t d,
	 Node* p = nullptr, uint8_t idx = 0)
      : box(b), start(s), count(c), depth(d),
	isLeaf(true),
	parent(p), childIdx(idx)
    {}
    
    /**
     * @brief In-place partitioning followed by recursive subdivision.
     * @param particles    Reference to the full particle array.
     * @param minParticles Minimum particle count required to split an internal node.
     * @param maxDepth     Maximum recursion depth.
     * @param depth        Current recursion depth.
     */
    void subdivide(ParticleOctree&           tree,  
		   std::vector<ParticleDataForTree>& particles,
		   size_t                               minParticles,
		   size_t                               maxDepth,
		   size_t                               depth,
		   float                                isoLevel = 0.,
		   bool   isIsoDensity = false,
		   bool   force = false);
  };

  /// Return a reference to the root node.
  const Node& root() const { return *root_; }

  std::vector<Node*> getAllLeafNodes() const {
    std::vector<Node*> leaves;
    collectLeaves(root_.get(), leaves);
    return leaves;
  }

  const std::vector<ParticleDataForTree>& getParticles() const {
    return particles_;
  }

  static void computeValueRange(const ParticleDataForTree* particles,
				std::size_t                 count,
				float&                      outMin,
				float&                      outMax)
  {    
    outMin =  std::numeric_limits<float>::max();
    outMax = -std::numeric_limits<float>::max();

    for (std::size_t i = 0; i < count; ++i) {
      float v = particles[i].val;
      if (v < outMin) outMin = v;
      if (v > outMax) outMax = v;
    }

    if (count == 0) 
      outMin = outMax = 0.0f;
  }
  
  void balanceTree(bool isIsoDensity);
  void querySphere(const glm::vec3& center,
		   float            radius,
		   std::vector<const ParticleDataForTree*>& out) const;

  std::vector<Node*> findAllNeighbors(const Node* leaf, int dir) const;
  const Node* findLeafContainingRoot(const glm::vec3 &p);

  // Recursively traverse nodes and print debug information.
  void dumpNodeRoot(void){
    dumpNode(root_.get(), 0);
  }

private:
  /**
   * @brief Helper that recursively builds a Node.
   * @param box    Spatial range for this node.
   * @param start  Start index in particles_.
   * @param count  Particle count.
   * @param depth  Current recursion depth.
   * @return       Owning pointer to the newly built Node.
   */
  std::unique_ptr<Node> buildNode(const BoundingBox& box,
				  size_t             start,
				  size_t             count,
				  size_t             depth,
				  bool               isIsoDensity = false);

  static void collectLeaves(Node* node, std::vector<Node*>& out) {
    if (!node) return;
    if (node->isLeaf) {
      out.push_back(node);
    } else {
      for (auto& c : node->children)
	collectLeaves(c.get(), out);
    }
  }

  Node* findNeighbor(Node* root, const BoundingBox& box, int direction);
  
  void querySphereRecursive(const Node*                     node,
			    const glm::vec3&                center,
			    float                           radius2,
			    std::vector<const ParticleDataForTree*>& out) const;

  
  std::vector<ParticleDataForTree> particles_;     ///< Centralized particle storage.
  float                              isoLevel_;
  size_t                             minParticles_;  ///< Threshold for splitting internal nodes.
  size_t                             maxDepth_;      ///< Maximum recursion depth.
  std::unique_ptr<Node>              root_;          ///< Root node.

  void collectFaceLeaves(Node* sib, int dir, const BoundingBox& origBox, std::vector<Node*>& out) const;
  
  struct NodeKey {
    uint32_t level;
    uint64_t spatial;
    bool operator==(NodeKey const &o) const noexcept {
      return level == o.level && spatial == o.spatial;
    }
  };

  struct NodeKeyHash {
    size_t operator()(NodeKey const &k) const noexcept {
      // Simple hash combine.
      return std::hash<uint64_t>()(k.spatial)
           ^ (std::hash<uint32_t>()(k.level) << 1);
    }
  };

  bool contains(const Node* n, const glm::vec3 &p);
  const Node* findLeafContaining(const Node* node, const glm::vec3 &p);
  void dumpNode(const ParticleOctree::Node* node, int indent = 0)
  {
    std::cout << std::string(indent, ' ');
    std::cout << "Level=" << node->depth;
    if (node->isLeaf)
      {
        // Compute the bounding-box center.
        glm::vec3 center = (node->box.min + node->box.max) * 0.5f;
        std::cout << " [Leaf] Center=("
                  << center.x << ","
                  << center.y << ","
                  << center.z << ")";

        std::cout << "\n";
      }
    else
      {
        std::cout << " [Internal]\n";
        // Recurse into child nodes.
        for (const auto& childPtr : node->children)
	  {
            if (childPtr)
	      {
                dumpNode(childPtr.get(), indent + 2);
	      }
	  }
      }
  }

};
 
