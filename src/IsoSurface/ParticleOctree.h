// ParticleOctree.h
#pragma once
#include <vector>
#include <memory>
#include <array>
#include <glm/glm.hpp>

#include <cstdint>
#include "DensityEstimator.h"
#include "main.h"

struct ParticleDataForTree {
  glm::vec3 pos;
  float     val;
};

struct BoundingBox {
  glm::vec3 min, max;
  bool contains(const glm::vec3& p) const {
    return (p.x>=min.x && p.x<=max.x
	    && p.y>=min.y && p.y<=max.y
	    && p.z>=min.z && p.z<=max.z);
  }
};


/// 全粒子を一元管理し、Octreeノードはインデックスのみ保持する設計
class ParticleOctree {
public:
  /// コンストラクタ：粒子をムーブし、空間全体(worldBox)をカバーするルートを構築
  ParticleOctree(TrackingVector<ParticleDataForTree>&& allParticles,
		 const BoundingBox&             worldBox,
		 float                          isoLevel,
		 size_t                         minParticles = 8,
		 size_t                         maxDepth     = 20);

  /// ノード構造体（内部/葉判定とパーティション情報のみ持つ）
  struct Node {
    BoundingBox                             box;       ///< このノードの空間範囲
    size_t                                  start, count;     ///< 粒子数
    size_t                                  depth;
    std::array<std::unique_ptr<Node>, 8>    children;  ///< 子ノードポインタ
    std::array<float,8>                     edgeValues;
    bool                                    isLeaf;    ///< 葉ノードかどうか
    float                                   minValue, maxValue;  

    std::atomic_flag subdivided_flag = ATOMIC_FLAG_INIT;

    bool try_mark_subdivided() {
      return !subdivided_flag.test_and_set(std::memory_order_acquire);
    }
    
    Node*     parent    = nullptr;  // 親ポインタ
    uint8_t   childIdx  = 0;        // 親から見た自分の子番号 (0..7)
    Node(const BoundingBox& b, size_t s, size_t c, size_t d,
	 Node* p = nullptr, uint8_t idx = 0)
      : box(b), start(s), count(c), depth(d),
	isLeaf(true),
	minValue(FLT_MAX), maxValue(-FLT_MAX),
	parent(p), childIdx(idx)
    {}
    
    /**
     * @brief in-place パーティショニング＋再帰分割
     * @param particles   全粒子配列への参照
     * @param minParticles 内部ノードを分割する最小粒子数閾値
     * @param maxDepth     再帰の最大深さ
     * @param depth        現在の深さ
     */
    void subdivide(ParticleOctree&           tree,  
		   TrackingVector<ParticleDataForTree>& particles,
		   float                                isoLevel,
		   size_t                               minParticles,
		   size_t                               maxDepth,
		   size_t                               depth,
		   bool   force = false);
  };

  /// ルートノードへの参照取得
  const Node& root() const { return *root_; }

  TrackingVector<const Node*> getLeafNodes(float isolevel) const {
    TrackingVector<const Node*> leaves;
    collectIsoLeaves(root_.get(), isolevel, leaves);
    return leaves;
  }

  TrackingVector<Node*> getAllLeafNodes() const {
    TrackingVector<Node*> leaves;
    collectLeaves(root_.get(), leaves);
    return leaves;
  }

  const TrackingVector<ParticleDataForTree>& getParticles() const {
    return particles_;
  }

  static void computeValueRange(const ParticleDataForTree* particles,
				std::size_t                 count,
				float&                      outMin,
				float&                      outMax)
  {    
    outMin =  FLT_MAX;
    outMax = -FLT_MAX;

    for (std::size_t i = 0; i < count; ++i) {
      float v = particles[i].val;
      if (v < outMin) outMin = v;
      if (v > outMax) outMax = v;
    }

    if (count == 0) 
      outMin = outMax = 0.0f;
  }
  
  void balanceTree();
  void querySphere(const glm::vec3& center,
		   float            radius,
		   TrackingVector<const ParticleDataForTree*>& out) const;

  void debugPrintRanges() const;
  
  TrackingVector<Node*> findAllNeighbors(const Node* leaf, int dir) const;
  const Node* findLeafContainingRoot(const glm::vec3 &p);

  // 再帰的にノードを辿って情報を出力する
  void dumpNodeRoot(void){
    dumpNode(root_.get(), 0);
  }

  
private:
  /**
   * @brief 再帰的に Node を構築するヘルパー
   * @param box    このノードの空間
   * @param start  particles_ 内の開始インデックス
   * @param count  粒子数
   * @param depth  現在の深さ
   * @return       新規構築した Node の所有ポインタ
   */
  std::unique_ptr<Node> buildNode(const BoundingBox& box,
				  size_t             start,
				  size_t             count,
				  size_t             depth);


  void evaluateDensityForAllLeaves(void);  
  
  void computeValueRangeRoot() {
    if(_flag_value_evaluated == false){
      evaluateDensityForAllLeaves();      
      _flag_value_evaluated = true;
    }
    
    computeRangeRecursive(root_.get());
  }
  
  void computeRangeRecursive(Node* node) {
    if (node->isLeaf) {
      // 葉は粒子自体を走査
      float mn = +FLT_MAX, mx = -FLT_MAX;
      for (int i=0;i<8;i++) {
	mn = std::min(mn, node->edgeValues[i]);
	mx = std::max(mx, node->edgeValues[i]);
      }
      node->minValue = mn;
      node->maxValue = mx;
    } else {
      // 内部は子から集約
      float mn = +FLT_MAX, mx = -FLT_MAX;
      for (auto& c : node->children) {
	computeRangeRecursive(c.get());
	mn = std::min(mn, c->minValue);
	mx = std::max(mx, c->maxValue);
      }
      node->minValue = mn;
      node->maxValue = mx;
    }

    /*if (node->isLeaf) {
      // 葉は粒子自体を走査
      auto begin = &particles_[node->start];
      auto end   = begin + node->count;
      float mn = +FLT_MAX, mx = -FLT_MAX;
      for (auto p = begin; p != end; ++p) {
	mn = std::min(mn, p->val);
	mx = std::max(mx, p->val);
      }
      node->minValue = mn;
      node->maxValue = mx;
    } else {
      // 内部は子から集約
      float mn = +FLT_MAX, mx = -FLT_MAX;
      for (auto& c : node->children) {
	computeRangeRecursive(c.get());
	mn = std::min(mn, c->minValue);
	mx = std::max(mx, c->maxValue);
      }
      node->minValue = mn;
      node->maxValue = mx;
      }*/
  }


  static void collectLeaves(Node* node, TrackingVector<Node*>& out) {
    if (!node) return;
    if (node->isLeaf) {
      out.push_back(node);
    } else {
      for (auto& c : node->children)
	collectLeaves(c.get(), out);
    }
  }

  
  static void collectIsoLeaves(const Node* node,
			       float isoLevel,
			       TrackingVector<const Node*>& out)
  {
    if (!node) return;
    // レンジ外ならサブツリー丸ごとスキップ
    //printf("min=%g max=%g\n", node->minValue , node->maxValue );
    
    if (isoLevel < node->minValue || isoLevel > node->maxValue)
      return;

    if (node->isLeaf) {
      out.push_back(node);
    } else {
      for (auto& c : node->children)
	collectIsoLeaves(c.get(), isoLevel, out);
    }
  }

  Node* findNeighbor(Node* root, const BoundingBox& box, int direction);
  
  void querySphereRecursive(const Node*                     node,
			    const glm::vec3&                center,
			    float                           radius2,
			    TrackingVector<const ParticleDataForTree*>& out) const;

  
  TrackingVector<ParticleDataForTree> particles_;     ///< 全粒子データを一元管理
  float                              isoLevel_;
  size_t                             minParticles_;  ///< 内部ノード分割に使う閾値
  size_t                             maxDepth_;      ///< 再帰の最大深さ
  std::unique_ptr<Node>              root_;          ///< ルートノード

  bool _flag_value_evaluated = false;
  
  void collectFaceLeaves(Node* sib, int dir, const BoundingBox& origBox, TrackingVector<Node*>& out) const;
  
  struct NodeKey {
    uint32_t level;
    uint64_t spatial;
    bool operator==(NodeKey const &o) const noexcept {
      return level == o.level && spatial == o.spatial;
    }
  };

  struct NodeKeyHash {
    size_t operator()(NodeKey const &k) const noexcept {
      // シンプルに combine
      return std::hash<uint64_t>()(k.spatial)
           ^ (std::hash<uint32_t>()(k.level) << 1);
    }
  };

  bool contains(const Node* n, const glm::vec3 &p);
  const Node* findLeafContaining(const Node* node, const glm::vec3 &p);
  
  std::unique_ptr<IDensityEstimator> densEstimator_;

  void dumpNode(const ParticleOctree::Node* node, int indent = 0)
  {
    std::cout << std::string(indent, ' ');
    std::cout << "Level=" << node->depth;
    if (node->isLeaf)
      {
        // バウンディングボックスの中心を計算
        glm::vec3 center = (node->box.min + node->box.max) * 0.5f;
        std::cout << " [Leaf] Center=("
                  << center.x << ","
                  << center.y << ","
                  << center.z << ")";

        // leaf の各頂点値
        std::cout << " edgeValues={";
        for (size_t i = 0; i < node->edgeValues.size(); ++i)
	  {
            std::cout << node->edgeValues[i];
            if (i + 1 < node->edgeValues.size()) std::cout << ",";
	  }
        std::cout << "}\n";
      }
    else
      {
        std::cout << " [Internal]\n";
        // 子ノードを再帰
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
 
